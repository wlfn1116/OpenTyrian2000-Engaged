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
| Play with a friend | Main menu > Online Multiplayer |
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

The start screen takes a seed, a run mode, and the base level rule.

- A blank seed generates a random one.
- The same seed and choices reproduce levels, music, courses, shops, and perks.
- Combat randomness is not seeded.
- **Base Level** decides whether a Chart-a-Course slate offers a level per route
  (**Varied**) or one level for all of them (**Same**); see Chart-a-Course. It is
  fixed for the run, and the two rules keep separate records.

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
Sector modifiers override it: Elite Pack forces 50%, Apex Swarm makes everything
elite or champion, Legion makes everything a champion, No Champions demotes
champions to elites, No Elites removes both, Giant Killer strips their health
multiplier, and Clean Signals strips their weapon bonuses.

### Sector modifiers

Course cards list threats in red and boons in green. Hostile modifiers raise
danger and payout together. Most boons lower one or both.

A card names the sector itself rather than its parts, and describes each
modifier by its effect, so the names below are the ones Debug Mode's Endless
Effects list uses. `Pays` is a multiple of the base clear reward, so `+1.0` adds
one base reward.

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
| Apex Swarm | 100% special enemies | +4.0 |
| Legion | 100% champions | +5.0 |
| Overclock | Faster enemy fire, shots, and scrolling | +1.6 |
| Overload | Stronger Overclock | +3.0 |
| Slipstream | Faster scrolling | +0.6 |
| Warp Speed | Much faster scrolling | +2.0 |
| Light homing | Weak enemy homing | +0.6 |
| Kamikaze | Moderate enemy homing | +1.2 |
| Rampage | Strong homing and more contact damage | +5.0 |
| Topsy Turvy | Flips the playfield and controls | +1.0 |
| Molasses | Slows the player ship | +1.5 |
| No Shield Regen | Disables shield recharge | +1.2 |
| Dead Generator | Disables shield recharge and starves the main gun | +3.0 |
| Martyrdom | Killed enemies fire a final radial burst | +1.8 |
| Seeker Rounds | Enemy shots make one delayed correction | +1.4 |
| Static Discharge | Damage drains and briefly disables generator recharge | +1.1 |
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
| Cursed Bounty | Large reward; next shop is empty | +4.0 |
| Turbodrive | Kills briefly speed up your guns | 0 |
| Overcharged | More player damage | 0 |
| Overdrive | Turbodrive and Overblast | 0 |
| Overblast | Kills stack player damage | 0 |
| Time Dilation | Slower enemy shots | 0 |
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

Radar reveals which level is underneath and adds a **Reroll** row below the
routes. One press redraws the whole chart: new levels, modifiers, payouts and
route count, and the zone it charts arrives with its own music. The row then
greys out for the rest of the visit, and stays grey across a save and reload or a
return to the same outpost after giving a zone up. The next outpost hands you a
fresh one. Surveyor adds routes. Recently played levels are avoided.

The **Base Level** row on the seed screen decides which levels a chart draws on.
**Varied**, the default, gives each route its own level. **Same** puts every route
of a chart onto one level, so the whole choice is which modifiers to fly it under;
the level itself still changes from one zone to the next. It is picked once per
run and fixed from there, and the two rules keep separate records (see Saving
below). Online it is a host setting, under **Endless Setup** in the lobby.

Queued Sabotage charges strip threats from the route you pick, and the card
updates before launch, including the lower payout.

### Outpost and E-Shop

The normal shop restocks randomly on every visit. Data Cubes becomes the E-Shop
entry, and Ship Specs becomes the perk list.

| E-Shop item | Base cost | Result |
| --- | ---: | --- |
| Shop Reroll | $6,000 + $1,000/zone | Replaces shop stock; later rerolls cost more |
| Sector Sabotage | $25,000 + $2,500/zone | Removes one threat from the selected course; up to three per outpost, shared between both players online |
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
| Radar | Reveals course level names, and rerolls one chart per outpost | 1 |
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

The **Base Level** rule splits the records the same way, since a chart of one
repeated level is a different run from a chart of five, and each rule gets its own
board.

The seed screen shows the deepest record for the mode and base level rule the two
rows above it are set to, across all difficulties, since the difficulty is picked
after.

All these records also have their own pages in **High Scores** on the title
screen, reached by paging right past the episode and Timed Battle boards to the
last three: **2 Player Campaign**, **Endless: Varied Base** and **Endless: Same
Base**. Each Endless page lists every run mode at both crew sizes with the zone it
stands at; the 2 Player Campaign page lists the best combined cash each episode
has been finished with online, and who was flying. That board is separate from the
two-player scores on the episode pages, which belong to Arcade.

Selecting a mode opens its breakdown by difficulty: a row for each of Easy,
Normal, Hard, Impossible, Suicide and Lord of Game, above them an **Any
Difficulty** row. A run sets the record for the difficulty it was started on, and
the **Any Difficulty** row simply shows the deepest of them, which is what the
mode list and the seed screen show.

The breakdown is where records are erased. Move to one with Up/Down and select
it. The page then names the record in full and asks **Are You Sure?**, opening on
**No, Keep It**, so reaching the wipe means deliberately moving down to **Yes,
Erase It** and selecting that. Esc unwinds one step at a time: out of the
question, then back to the mode list, then off the screen. An erased record
cannot be recovered.

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
weapon costs you nothing, since neither happens inside a zone. In an online run
either ship's custom weapon counts, even if your own **Custom Weapons** row is
off, and both machines mark their records alike.

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

### Arcade tweaks

**Setup > Enhancements > Game Tweaks > Arcade** holds the three arcade switches:
Life Boost, Random Pickups and Rear Gun Scale, all off by default. Online, the
host's three settings bind the session for both machines.

### Life Boost

Lives buy durability as well as retries, in 1 Player Arcade, 2 Player Arcade,
Online Arcade and on the Super Arcade secret ships. Each ship's shield
and armor ceilings scale off its own life count. At 1 life they are the vanilla
numbers, and at the 11-life maximum both gauges reach a full bar. Growth in
between is even, so six lives puts the Stalker at 22 armor and 19 shield.

| Mode | Ship | Armor at 1 life | Shield at 1 life | Either, at 11 lives |
| --- | --- | --- | --- | --- |
| 1 Player Arcade | Stalker | 15 | 10 | 28 |
| 2 Player (and Linked Online Arcade) | Silver Ship | 10 | 10 | 28 |
| 2 Player (and Linked Online Arcade) | Dragonwing | 10 | 10 | 28 |
| Separate Online Arcade and Timed Battle, both ships | Stalker | 15 | 10 | 28 |

Gaining a life raises both ceilings at once and carries damage across
proportionally, so a ship at half armor stays at half armor. Losing one shrinks
them, and the respawn refills both to the new ceiling instead of the vanilla
half. Armor pickups top up to the current ceiling rather than a flat 28, so a
light hull holds less at low life counts.

The Super Arcade ships each scale off their own hull, which runs from 8 to 30
across the nine. All nine carry the stock Gencore High Energy Shield, so their
shields climb 10 to 28 alike. The Nort-Ship Z's 30 hull is already past the bar
and stays there. SuperTyrian is excluded.

Off, every ship keeps its stock hull and shield.

The shield gauge's full-charge line is on both gauges of the two-player HUD now.

### Random Pickups

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

### Rear Gun Scale

On, your rear gun fires at its own collected power plus your lives minus one, so
a stocked-up run gets a wider rear spread without spending pickups on it. It
applies in 1 Player Arcade and in Separate Online Arcade, Timed Battle included.
The linked two-player
pair is excluded: player two's rear-gun power is also that ship's life counter,
so raising one would raise the other. SuperTyrian is excluded as well.

## Online play

**Online Multiplayer** on the main menu opens a screen of the same name: Host
Game, Find LAN Games, Join by IP Address, and Your Nickname, the name the other
player sees. The game uses UDP port 1333 unless the host changes it.

The menu refuses to start a netgame while the FPS Cap is below 35: the simulation
runs at 35 Hz, and a lower render cap drags both players down to the capped
machine's rate. Set the cap to 35 or higher, or Uncapped.

**Host Game** covers the listen port, game type, Mode, episode or Endless Setup,
difficulty, Host Flies or Credit, Game Speed, Netcode, and Desync
Recovery, one row each with the current value on the right and a line explaining
the highlighted row underneath. The rows adapt to the game type: Destruct swaps
the episode and difficulty rows for its own Battle Mode row, and Arcade's Timed
Battle mode swaps the episode row for a Level one.
The host's choices bind the session for both machines. While the other player
waits for the host to start, they see the same list. The joiner's own settings
are left alone and restored afterwards.

| Lobby row | What it does |
| --- | --- |
| Listen Port | The UDP port this machine listens on, 1333 unless you change it. Always shown, and the joiner must use the same one. |
| Game Type | **Arcade** plays the arcade rules in one of three shapes (see Mode). **Campaign** runs a full episode with two independent, fully equipped ships, cash, and shops. **Endless** runs the Endless roguelite with the same two ships. **SuperTyrian** and **Super Arcade** fly the two one-player rulesets with a ship each (see below). **Destruct** fights the artillery mini-game head to head (see Online Destruct). |
| Mode | Arcade only. **Linked** (default) is the classic pair, the Silver Ship and the Dragonwing sharing one HUD and able to dock. **Separate** gives each player their own Stalker, the ship a solo arcade run flies: two of the single-player arcade game running side by side in one level, each with its own HUD, lives, guns, sidekicks, special, superbombs and score. Balls, weapons and powerups belong to whoever flies into them. **Timed Battle** is Separate ships racing one of the three Timed Battle levels for cash (see Online Timed Battle). |
| Battle Mode | Destruct only. Which of the five battles the session fights: 5-Card War, Traditional, Heli Assault, Heli Defense, or Outgunned. The config-file Custom mode stays offline: it is built from each machine's own file, so the two sides would field different armies. |
| Episode | Starting episode for a new game. Only episodes installed on the host are offered. Endless always begins at episode 1 and travels on from there, so the row is replaced by Endless Setup. Destruct has no episodes and hides the row. |
| Level | Timed Battle only, in place of Episode. Which of the three battles both players race. The episode it belongs to comes with it. |
| Endless Setup | Endless only. Opens a page with the run seed, the run mode, the base level rule, who charts each course, and whose drive streak a kill feeds. |
| Difficulty | Starting campaign or Endless difficulty. Linked arcade applies its usual two-player difficulty adjustment. Every game type that gives each player a whole ship (Separate arcade, Timed Battle, Campaign, Endless, SuperTyrian and Super Arcade) plays exactly the rung chosen here, with no adjustment. |
| Variant | SuperTyrian only, in place of Difficulty. **Standard** is the usual run; **Scrollock** is the gentler one, the same choice solo SuperTyrian makes from the Scroll Lock key. SuperTyrian has no difficulty ladder. |
| Host Flies | Which ship the host takes: the Silver Ship or the Dragonwing. Linked arcade only, and remembered between sessions. Separate arcade and Timed Battle give both players the same ship, SuperTyrian and Super Arcade settle their ships themselves, and Campaign and Endless give both slots the same kind of ship, so the row is hidden and the host flies as player one. In Destruct the row is titled **Host Fights On** and reads **Left Side** or **Right Side**: the side of the battlefield the host mans, with the joiner on the other. |
| Credit | Campaign and Endless, in place of Host Flies. **Shared** (default) pays every kill and every score pickup to both players at its full value, so you each end the level with the same earnings and neither has to hang back. **Individual** pays a kill to whoever's shot destroyed the enemy and a pickup to whoever flew into it. In Endless, Individual splits what one player would have earned alone between two wallets, so it is the harder economy on purpose. |
| Double Earnings | Individual credit only, so the row is not shown under Shared. **On** pays combat income twice over to whoever earned it: score pickups, kill cash, and elite and champion bounties alike. That puts a split take back near what one player alone would have collected. Zone clear bonuses and bank interest are paid at face value. |
| Game Speed | Session speed for both players. It does not appear in the in-game Esc menu online, so the lobby choice is final. Destruct hides the row and always plays at Normal, so both machines count their frames at the same rate; your speed for the other game types is left untouched. |
| Netcode | **Rollback** (default) applies your input the instant you press it and quietly corrects the other ship when its input arrives. **Delay-Based** is the original lockstep, whose input lag grows with ping. Destruct takes this row too, with a rollback of its own (see Online Destruct). |
| Desync Recovery | On by default. If the two machines drift apart, the game pauses for a moment, the host sends its whole game state over, and both continue from the host's version. Needs rollback netcode and two builds of the same version; it gives up after three repairs in one level. The row is only there while Netcode is Rollback: Delay-Based cannot detect a desync in the first place, so it hides the row and turns the setting off. Destruct hides it as well, because the state the host would send is the main game's and not the minigame's. |

**Join by IP Address** takes an address on its own or with a port, like
`123.45.67.89:1337`. Ctrl+V pastes over whatever is in the field. It comes back
pre-filled with the last address you used, restarts included, so rejoining the
same host is Join then Enter.

The outpost help bar shows a **Ping** figure at its right end, updated about
every one and a half seconds and reading `--` until the first reply. It is
dropped on rows that already have something at that edge, a price or a stack
count among them, rather than being pushed up against the sentence. Under
roughly 85 ms the game runs at full speed on the default network delay of 3.
Above that it starts to slow, and raising the delay trades input lag for
smoothness.

Options in the outpost is the ordinary options page with **Load Game** removed:
loading mid-session would leave the other machine playing something else. Save
Game, the volume and sensitivity sliders, and the joystick, keyboard, and mouse
setup screens all work as usual, and backing out of any of them returns here.

Online Arcade with Linked ships keeps the split two-player sidebar, tags each
gauge block **P1** or **P2**, and dims the other player's gauges. Separate ships,
Online Campaign and Online Endless give each machine the normal one-player sidebar
for its local ship. Both player names and cash totals remain visible along the
bottom of the playfield. On levels dark enough to light the playfield with a
cone from the ship, every online mode anchors the cone to the ship you fly:
the other ship stays in the dark on your screen and lights itself on theirs.

**Online SuperTyrian** is two SuperTyrian runs side by side: both players fly the
Stalker 21.126 with the Atomic RailGun, and the SuperTyrian twiddle combos work
for each ship independently, exactly as in the solo mode. There is no difficulty
row; the Variant row picks Standard or Scrollock for both players. Everything
else plays like Separate arcade: own lives, own guns, own score.

**Online Super Arcade** starts with a ship-picking screen instead of a lobby ship
setting. After the host presses Start, each player chooses their own ship from
the nine Super Arcade ships, with the highlighted hull drawn below the list; move
with the arrow keys or the mouse and confirm with Enter or a click. The two picks
are independent and may match. Whoever chooses first sees "Waiting for the other
player..." and then which ship their partner picked. A choice is not final while
you are still waiting: Esc (or a right-click) takes it back so you can pick a
different ship, right up until your partner chooses and the game starts. Esc
before choosing anything leaves the session instead. Weapon balls keep the same
colors on every ship, so when both of you fly into the same color, each ship
gets the weapon its own arsenal keeps in that slot, and the paired special (both
variants, switched with the rear-mode key) belongs to each ship separately.

In Campaign, each player chooses and powers up a complete ship independently:
front and rear weapons, sidekicks, generator, shield, hull, special, cash, and
weapon mode. The Arcade link, Dragonwing role, shared power rules, and lives do
not apply. Between levels, both players can use their own shop at the same time.
Purchases are sent to the peer as they are committed. Whoever picks a level first
waits at "Waiting for other player." while the other finishes outfitting; nobody
is pulled out of the outpost early. Where the route offers a choice of planets and
the two of you pick different ones, the host's choice is the one you both fly.

While you are waiting, **Esc** takes you back into the outpost to change
equipment or pick a different planet. The waiting screen says so, and it holds
for as long as you are both still in the outpost: your partner picking their
level does not take the option away from you. The one moment it does not answer
is the last one, once your partner has left the outpost and is waiting on the
level itself; there is nobody left to call back, so the level loads instead.

The hidden mini-games work online, and both ships fly them as equals: each of
you gets the same issued Stalker 21.126 with the mini-game's own kit and three
lives. Where the route offers **\*\* ALE \*\*** or **SQUADRON**, the host's
planet pick decides for the pair like any other disagreement; **TIME WAR** only
shows itself when both ships have the Stalker 21.126 equipped, never for just
one. Dying, or quitting from the Esc menu, puts both of you back at the outpost
you launched from with ships, cash, and rules exactly as they were, so a
mini-game visit costs the pair nothing. \*\* ALE \*\* and SQUADRON are endless,
and that is the only way out of them. TIME WAR can be cleared, and clearing it
does what it does in single player: the rest of the campaign carries on as a
SuperTyrian run, flown with whatever each of you cleared it with.

Custom weapons work in Campaign and Endless, one design each. Your design is
sent to the other machine on the way out of the outpost, so both of you fly and
see the real thing. Sending it happens once per changed design and can hold the
"Waiting for other player." screen for a moment on a large weapon.

At the start of each level both machines wait for each other. A slower loader
shows "Waiting for other player." before the two fade in together.

**P does not pause an online game**, in any game type, Destruct included, and
neither does clicking away to another window: your ship keeps flying while you
are not looking at it. Only one machine would have stopped, and the other player
would be left holding a session that had gone quiet. Esc still opens the in-game
menu, which is the way out of a level.

### Online Endless

The Endless lobby flies the roguelite with two ships. One run, one sector, one
zone counter; wallets, stock and gear belong to one player each.

| Endless Setup row | What it does |
| --- | --- |
| Seed | A named seed repeats a run exactly. Leave it blank and the run rolls its own, which the joiner's summary screen shows you once the session starts. |
| Run Mode | **Relaxed**, **Standard** or **Hardcore**, exactly as in a solo run. Relaxed opens the death menu when both ships are down, Standard ends the run there, and Hardcore does that and saves nothing. |
| Base Level | **Varied** (default) gives each charted route its own level; **Same** puts every route of a chart onto one level, leaving the modifiers as the whole choice. Exactly as in a solo run, and fixed for the session. |
| Charts Course | Who picks the next sector: **Host**, **Guest**, **Alternating** (turn about, and the turn is kept in the save), or **50-50** (a coin flip from the run seed, so both machines land on the same side of it). |
| Combo Feed | Whose kill-fire streak a kill feeds. **Individual** (default) counts a kill for the ship whose shot destroyed it, so a drive you paid for is worth what your own shooting earns. **Shared** has every kill feed both streaks. A kill neither of you can be credited with feeds both either way. |

Both of you shop at the same outpost at the same time, each with your own stock,
your own prices and your own wallet. Rerolling changes only your own shelves, and
one player's rerolls and gambles never move what the other is dealt. Almost
everything you buy is yours alone: guns and gear, Reinforce hull tiers, a Revive
token, bombs, a Special Weapon, and the kill-fire drives.

Turbodrive, Overblast and Overdrive belong to the ship that paid for them. One of
you can be flying Turbodrive while the other stacks Overblast, each with their own
window and their own combo, and the buyer's ship glows in that drive's colour on
both screens. The kill-fire readout in the corner is your own drive's. Under the
default Individual combo feed your streak counts your own kills, so a drive is
worth what you shoot with it; a sector that deals a drive still deals it to both
of you.

Perks are personal. You each pick from your own slate, and a stack works on the
ship that took it and no other, up to that perk's normal maximum on each of your
rows. Your Perks list, the Owned counts on a perk offer, and the surcharge an
**Extra Perk** costs all read your own collection. Two perks act on a screen you
share rather than on a ship: **Surveyor** widens the course slate when its owner is
the one charting, and **Radar** names the levels for the player holding it. Radar's
chart reroll goes with the charting seat, so it is the charting player's Radar that
offers one, and both of you fly whatever it redraws. One buy
still reaches further than the ship that made it: **Sector Sabotage** strips a
danger off the sector you both fly, and sabotage charges from the two of you add up
to the same three-strip cap. Once that cap is met the row reads **Sabotage Maxed**
for both of you, whoever paid, so neither buys a strip the other has already
covered. You each keep your own escalating price, so splitting the purchases
between you costs less than one of you buying them all. The queued count on the
help line is the pair's.

A gamble is yours: the cash, gear and drives it hands out or takes away are the
gambler's. Its rarer outcomes change the next sector instead (a shop discount, a
bulked-up boss, a rush of rammers), and those land on the pair.

Only one of you charts, so the other's Start Level puts up "Partner is charting a
course." and waits there for the sector they pick. Esc goes back into the outpost
for as long as that wait lasts, so nothing is committed by looking, and both of
you keep shopping until you are ready.

Quitting a zone from the in-game menu takes both of you back to the same outpost
you launched from. Neither player banks the zone: it was given up, not finished.

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

**Quit** in the in-game menu means the same thing online as it does alone: the
zone is abandoned and both of you go back to the outpost you launched it from,
with the loadout you launched it with. It does not end the run or the session.

### Online Destruct

The Destruct game type puts the two of you on opposite ends of the artillery
mini-game: the host mans the side the lobby's Host Fights On row names, the joiner
gets the other, and the Battle Mode row picks which of the five battles you
fight. There are no menus after the lobby: once the joiner connects, both
machines land on the Destruct title card, which repeats the battle, your side,
and your opponent's name. The card is a barrier: press any key (or a controller
button) when you have read it, and the first map fades in once **both** of you
have. The two lines at the bottom say where that stands (your own state above,
the other player's below), so a card that will not move is telling you who it is
waiting on. There is no time limit.

Esc steps back before it steps out: once you have readied, the first press takes
that ready back, and only a second one leaves. The line at the bottom says which
it is about to do. Leaving ends the session for both of you, which is the way
out when the other player has walked away from their card.

Both machines generate every map from a seed the host rolled for the session,
so you fight on the same terrain, under the same walls, to the same randomly
chosen soundtrack. When one side's last installation falls, the survivor scores
the battle's points, and after a short beat the next round starts on fresh
terrain. Scores accumulate for as long as the session runs, exactly like the
local game.

Controls are the local game's, with one convenience: both keyboard layouts
steer **your own side**, so the arrow-key layout and the C/V/A/Z layout work no
matter which side you man, and a controller does too. The function keys behave
online as follows:

| Key | Online meaning |
| --- | --- |
| P | Disabled online: pause is offline-only here, the same rule as every other game type. |
| Backspace | New round for both players, current scores kept: the same map-reroll it is locally. |
| Esc | Ends the session for both players, back to their own menus. A controller's pause button does the same. |
| F1, F10, F11 | Disabled online: the help screen would stall the connection, and the CPU toggles would split the simulation. |

Destruct honours the lobby's Netcode row. On **Rollback**, the default, your own
aiming and firing answer the moment you press a key, and the other side's units
and shells are predicted and quietly corrected when their input arrives; a
correction replays the battle, terrain damage included, so a crater or a kill can
change in the frame after it appeared. On **Delay-Based** both sides' input lands
after the session's network delay instead, which never corrects anything but puts
that delay on your own controls. Leaving the round or the session (Backspace and
Esc) waits for both machines either way, so neither can reroll a map alone.

Desync Recovery is unavailable here: the state a host would send over is the main
game's, and the minigame is no part of it. If the two battles do drift apart, the
session says so in the network log and plays on.

Destruct sessions play at Normal game speed on both machines, so their frame
counters keep step; the lobby's Game Speed row is hidden for that reason and your
speed for the other game types is left alone. Nothing about a Destruct session is
saved except the two lobby rows (the Battle Mode and which side you man), which
are remembered for the next time you host.

### Online Timed Battle

Setting the lobby's Mode row to **Timed Battle** turns Arcade into a race. You
both fly the Stalker of Separate arcade, in the battle level the Level row
names, and the level is the clock: whoever has banked the most cash when it runs
out wins. There is no shared purse and no help from the other ship, and there is
only one of each pickup on the field, so a ball you take is a ball they do not.

Once the joiner connects, both machines land on a Timed Battle card naming the
level, the difficulty, which player you are, and who you are racing. It is a
both-ready gate like the Destruct title: press any key when you are ready, and
the battle starts once the other player has too. Esc works the same way it does
there, taking back your ready first and leaving the session on a second press,
so you can change your mind or back out while the other side is still reading.

A Timed Battle is never offered Load Game, and it does not write to the Timed
Battle high-score boards: those hold one name per score, and a race produces two.
It ends on a scoreboard of its own instead, with both totals and who took the
race, whether the clock ran out or you both went down first. Level totals work
as they do solo, both bonuses paid to each ship: the time left on the clock when
the level is cleared, and a thousand a life for whatever each of you had left.

The Mode row and the Level row are remembered for the next time you host.

### Saving and resuming an online game

Save from the shop with Options > **Save Game**, or Alt+S while no purchase
preview is open. The page's last slot is written automatically at the start of
every level as `LAST LEVEL`; save into a numbered slot to keep a separate run.
Both machines write their own copy after exchanging their latest shop state, so
either player can host the resume. If the other machine goes quiet while that
exchange is in flight, the save is written anyway after a few seconds rather than
leaving you stuck on it; their ship is then stored as of the last state you had
from them, so save again once they are back.

Linked arcade saves remain compatible with the regular local two-player page.
Separate arcade, SuperTyrian and Super Arcade saves carry both complete ships,
lives and rulesets included (a Super Arcade save remembers which ship each of you
picked), and can only be resumed through the same game type that wrote them; the
other arcade lobbies and local play show them dimmed. Campaign and Endless saves
carry both players in full: each ship and its front and rear weapons with their
power levels, both sidekicks, generator, shield, special, rear-gun firing mode,
and cash total, plus the shared episode, difficulty, data cubes, and next-level
position. Nothing either of you bought is dropped. They can only be
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

In Online Campaign and Online Endless each player brings their own design. The
two are kept apart, so you can both fly a custom weapon in the same session
without either of you seeing the other's in your shop.

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
A zone jump in Online Endless moves both players to the chosen zone together.
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
| OPL3 | None; default |
| FluidSynth | A SoundFont file (`.sf2`, `.sf3`, `.sf`) |
| Native MIDI | Windows x86-64 |

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
