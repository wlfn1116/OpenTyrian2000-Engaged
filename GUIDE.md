# OpenTyrian2000 Engaged player guide

This guide covers the features added by Engaged. The original Tyrian campaigns
and controls work as before.

## Menu map

| Task | Menu |
| --- | --- |
| Start Endless | Main menu > 1 Player Endless |
| Play online | Main menu > Online Multiplayer |
| Change online ship colors, opacity, or HP bars | Online outpost > Customize |
| Enable smooth rendering | Setup > Graphics |
| Play as close to the original as possible | Setup > Enhancements > Preset |
| Design a weapon | Setup > Enhancements > Weapons > Weapon Creator |
| Change health bars or gauges | Setup > Enhancements > Heads-Up Display |
| Turn the touch sidekick buttons on or off | Setup > Enhancements > Heads-Up Display |
| Configure restored content | Setup > Enhancements > Weapons or Gameplay |
| Choose a music backend | Setup > Sound > Music Synth |
| Turn on Debug Mode or network logs | Setup > Diagnostics |
| Open Destruct or SuperTyrian | Title screen > Extra |
| Use in-game cheats | Esc > Extra |

## Graphics

The useful settings are under **Setup > Graphics**.

- **Smooth Motion** presents interpolated frames at the display rate. In
  supported modes it also moves your own ship at that rate, online included.
- **Sub-pixel** renders the playfield at Auto, Off, 2x through 5x, or Native.
  Supersampled output uses unfiltered nearest-neighbor sampling. The shop's
  weapon preview renders at the same factor, so its ship, shots, and sidekicks
  move as smoothly as they do in play.
- **Native** follows the fitted output size. It costs more GPU time than the
  fixed modes, especially on high-resolution displays.
- **Sub-pixel FX** extends sub-pixel rendering to the ice, water, and lava
  effects. Turning it off renders the feedback and its source layers at native
  size, reducing the cost at 3x and above. Foreground objects stay sub-pixel.
  Console builds default this off.
- The Vita always resolves Sub-pixel to 1x.
- On Vita this setting is named **Smooth FX**. Turning it off updates the
  smoothie background once per 35 Hz simulation tick while foreground movement
  remains display-rate, avoiding repeated feedback filters on the same tick.
- **FPS Cap** accepts Left/Right steps or a typed number. Use 35 or higher for
  online play. A value of 0 means Uncapped.

The simulation still runs at 35 Hz. Demo recording and demo playback use
fixed-step ship movement even when Smooth Motion is enabled. Online play sets
its own rules for ship movement; see Online play.

## Endless mode

Endless builds an open-ended run from the shipped Tyrian levels. Difficulty
comes from depth scaling, special enemy tiers, and sector modifiers.

The loop is:

```text
outpost -> choose a course -> clear the zone -> outpost
```

Zone 100 rolls the credits and then continues.

The Orange Shield special orbits the ship in Endless. The campaign keeps its
shipped path, which circles a point above the hull.

### Starting a run

The start screen asks for a seed, run mode, and Base Level rule.

- A blank seed creates a random run.
- The same seed and choices reproduce levels, music, courses, shops, and perks.
- Combat randomness is separate from the run seed.

Base Level has four rules:

| Rule | Levels on one chart | How they are picked |
| --- | --- | --- |
| Varied | One per route | Drawn at random, avoiding the last few played |
| Varied Shuffle | One per route | Taken in order from a shuffled bag of every level |
| Same | One, repeated | Drawn at random, avoiding the last few played |
| Same Shuffle | One, repeated | Taken in order from a shuffled bag of every level |

Both Same rules vary only the modifiers across a chart. Both Shuffle rules hold
every eligible level in a bag: each chart takes the pieces it needs off the
front, and the bag refills with a fresh shuffle once it empties, so a run works
through the whole pool before any level repeats. A Radar reroll spends the hand
it discarded and deals the next pieces.

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

Normal difficulty reaches these landmarks:

| Zone | Change |
| ---: | --- |
| 25 | The rising tide begins adding enemy shots |
| 30 | The tide begins raising enemy damage |
| 40 | Roughly one enemy in four is elite or champion |
| 99 | Elites reach 6x health; bosses reach 20x; three enemies in five are special |
| 199 | Bosses reach 32x health; four enemies in five are special |
| 221 | Ordinary enemies reach their 12x health ceiling |

Health rises in fractional steps each zone. Champions make up about one tenth
of special enemies at the start, just under a third at zone 99, and seven tenths
at zone 199. Higher difficulties reach the same marks sooner: Hard reaches the
zone-99 mix at zone 83, while Suicide reaches it at zone 62.

| Tier | Health | Offense | Bounty |
| --- | --- | --- | ---: |
| Normal | Depth-scaled | Normal | None |
| Elite | 2x to 6x | +25% contact damage | $150 at zone 1, +$40 per zone, max $2,500 |
| Champion | 2x to 6x | Faster fire, +50% shot and contact damage | $600 at zone 1, +$170 per zone, max $11,000 |

Piercing shots pass through every hull they cross and can hit again while they
overlap a target. They carry little damage per hit, then gain their own depth
scaling plus your damage perks and drives. Bosses, elites, and champions have a
short repeat-hit lock against the same shot.

The Mega Cannon, Sonic Impulse, and Needle Laser always fire piercing shots.
Dragon Frost and Dragon Flame gain them at power 9.

Elites and champions are palette-shifted, and their sparks, shots, explosions,
and bounty line all carry the same tint. Linked parts share one tier and one
bounty, so a whole hull shifts together. Every enemy takes its tier on the frame
it appears, so nothing recolours or rearms while you are fighting it. Bosses and
enemies that start out invulnerable can be promoted; scenery that can never be
damaged cannot.

### Sector modifiers

Course cards show threats in red and boons in green. The payout column below is
a multiple of the base clear reward.

A sector sets one special-enemy share. Elite Pack, Apex Swarm, Legion, and No
Elites all decide it, so only the strongest one a sector carries is listed and
paid for. No Champions caps the tier instead of the share, so it can appear
alongside Elite Pack.

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
| Topsy Turvy | Flips the playfield, controls, and twiddle directions | +1.0 |
| Molasses | Slows the player ship | +1.5 |
| No Shield Regen | Disables shield recharge | +1.2 |
| Dead Generator | Disables shield recharge and starves the main gun | +3.0 |
| Martyrdom | Killed enemies fire a radial burst | +1.8 |
| Seeker Rounds | Enemy shots make one delayed correction | +1.4 |
| Twin Seekers | Enemy shots make two delayed corrections | +1.8 |
| Hunter Rounds | Enemy shots make one much wider correction | +2.1 |
| True Aim | Enemy shots re-aim exactly at the ship once | +2.7 |
| Kill Shot | Enemy shots re-aim exactly at the ship twice | +3.8 |
| Static Discharge | Damage drains and stalls the generator | +1.1 |
| Retaliation | Kills briefly speed up enemy fire | +1.5 |
| Backfire | Kills briefly jam your guns | +1.2 |
| Burnout | Backfire with stacking penalties | +1.8 |
| Misfire | Kills stack a player damage penalty | +1.4 |
| Overheat | Kills speed up your guns and damage the hull | +1.4 |
| Marked | Strengthens the next boss | Varies |
| Nitro | More player damage; any hit is fatal | Varies |
| Dud | Disables superbombs | Varies |

The five course-correction threats from Seeker Rounds through Kill Shot are
mutually exclusive.

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

The Base Level rule is fixed when the run starts. Each of the four rules keeps
separate records.

### Outpost and E-Shop

The ordinary shop restocks at each visit. Data Cubes opens the E-Shop and Ship
Specs opens the perk list.

The shop distinguishes guns that share a shipped name:

- Front Protron and Rear Protron;
- Front Multi-Cannon and Rear Multi-Cannon;
- Twin Vulcan Cannon for the twin-barrel version;
- Vulcan Cannon for the sweeping-fire version.

The Dragonwing appears only in the Endless shop. It flies as a full ship with
front and rear guns, and trailing sidekicks move outward to clear its wings.

Three hulls use their Super Arcade names: Nort Ship Z, TX SilverCloud, and
Pretzel Pete Truck.

| Item | Base cost | Effect |
| --- | ---: | --- |
| Shop Reroll | $6,000 + $1,000/zone | Replace your shop stock |
| Sector Sabotage | $25,000 + $2,500/zone | Remove one threat, up to three per outpost |
| Reinforce | $15,000 + $2,000/zone | Add 6 maximum armour |
| Extra Perk | $70,000 + $2,500/zone, plus perk surcharges | Open a four-choice perk pick |
| Special Weapon | Share of entry cash | Equip a random safe special |
| Turbodrive / Overblast / Overdrive | Share of entry cash | Add the chosen drive next sector |
| Revive | $150,000 + $10,000/zone | Survive one lethal hit |
| Bomb | $2,500 + $400/zone | Add one superbomb |
| Gamble | $25,000 + $2,000/zone | Apply a random good or bad result |

Repeated purchases can cost more. Kill-fire drive prices use the cash held on
entry to the outpost.

Extra Perk costs more for every stack you hold and every perk you buy. Buying
several at one outpost adds a surcharge until the next outpost. Co-op players
are priced separately.

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
| Bounty Hunter | Doubles elite and champion bounties, and pays 4x for score pickups | 1 |
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
| Kinetic Converter | Damage taken refunds power, recharge, and sidekicks; cheaper twiddles | 3 |
| Countermeasures | Clears nearby shots on every hull hit | 2 |
| Chain Reaction | Kills blast nearby enemies, and those blast on | 3 |
| Financier | More interest and lower shop prices | 4 |
| Ordnance Reserves | More sidekick ammo and special duration | 4 |
| Failsafe | Brief invulnerability after hull damage | 4 |
| Guidance Package | Shots home in; later stacks add sidekicks and specials | 4 |
| Twin Pods | Every sidekick volley fires twice, at double ammo and power | 1 |
| Reinforced Prow | Ramming deals 2x to 5x and costs you 78%, 56%, 34%, 12% | 4 |
| Knife Fight | +15% damage within 7 px of an enemy, fading out by 55 px | 4 |
| Deflector | Enemy shots cost 17% less shield per stack and bounce back; 2x return damage at two stacks | 2 |

A few perks need more detail than the table can hold.

**Guidance Package**

- Stack 1 steers main guns, stack 2 adds sidekicks, and stack 3 adds specials.
- Later stacks correct course more often.
- Guided shots retarget when their enemy dies.
- Pickups, scenery, invulnerable parts, and superbombs are ignored.

**Twin Pods**

- Each sidekick volley gains a second shot centered on the pod.
- The extra shot costs generator power and ammunition.
- A pod with one round left fires only its original shot.
- Charge sidekicks fire both shots at the same charge.

**Reinforced Prow**

- Stacks deal 2x, 3x, 4x, and 5x contact damage.
- Damage taken falls to 78%, 56%, 34%, and 12%, with a one-point minimum.
- Ram kills award normal drops, bounty, streak credit, and Chain Reaction.
- Opening Salvo and Knife Fight can raise ram damage.
- Invulnerability allows one unanswered ram hit every ten ticks.

**Knife Fight**

- Each stack adds 15% damage within 7 px of the enemy hull.
- The bonus fades to zero at 55 px.
- It applies to shots, specials, and rams, but not Chain Reaction.
- Blood effects show when the bonus is active.

**Deflector**

- A shot fully absorbed by shield returns along its incoming path.
- Returned damage equals the absorbed hit, doubled at two stacks.
- Hull hits, empty shields, and invulnerable hits return nothing.
- Bulwark reduces the absorbed amount before Deflector reads it.
- A return fired during Opening Salvo keeps the volley bonus.
- Each stack reduces shield loss from enemy shots by 17%.
- Shield overflow still deals full armor damage. Rams get no discount.

**Opening Salvo**

- Two seconds without main-gun fire charges a one-second window.
- The window gives 2.5x damage and removes generator cost.
- Sidekicks, specials, shotless effects, and rams receive the bonus.
- The generator gauge turns green when ready.
- Only main-gun fire spends the charge.

**Kinetic Converter**

- Each stack removes 8% of remaining special recharge after damage.
- Each stack restores 0.25 sidekick rounds and one charge stage.
- Shield loss refunds 20% of its generator value per stack.
- Twiddle shield or armor cost falls by 22% per stack, to a minimum of one.

**Countermeasures**

Every hull hit clears nearby enemy shots. A second stack widens the sweep;
shield-only hits do not trigger it. There is no cooldown.

**Chain Reaction**

- Kills blast nearby enemies. Later stacks add damage and radius.
- Player damage bonuses and penalties also affect the blast.
- Anything destroyed creates the next hop one tick later.
- Each wave hits a linked hull once, regardless of tile count.
- Tier health multipliers still apply.
- Wave kills award the same drops, cash, and bounty as direct kills.

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

No route on a multiple of 100 scrolls faster than usual, so Slipstream, Warp
Speed, Overclock, and Overload never appear on those charts.

Data cubes and secret-level orbs become random safe specials. They draw as a
color-cycling `?` and collect only on the glyph. Armored orbs keep their original
look until opened.

Random-special events become weapon power-ups. The aimed-bolt version of Pearl
Wind is labeled Pearl Shot; the flare keeps the Pearl Wind name.

### Death and saving

A revive restores the hull, clears enemy shots, and briefly stops enemy fire.
Without one, Relaxed offers Restart Zone, Return to Outpost, or End Run. Either
retry keeps what the outpost was paid for, including a drive bought for that
zone: the cash is spent, so the drive is still flying when the zone restarts.

Relaxed and Standard checkpoint at the outpost, into the same save slot as the
campaign half of the run (`opentyrian.sav`, see Files and logs). Hardcore never
writes a run save. Quit Level restores the launch snapshot. Such a slot reads
**Endless** on the Saved Games screen and `End` in the outpost's **Load Game**
and **Save Game** list, where a campaign save names its episode.

Records are split by run mode, difficulty, Base Level rule, and crew size. A
trailing `C` marks a run that used a custom weapon.

The **Endless** high-score page drills down from mode, to Base Level rule, to
difficulty. Records can be erased from the final list after confirmation.

## Shop

A gun with two fire modes is marked **Dual-Mode** beside its price in the rear
weapon list. Only that list marks it: the front bay always fires the first mode,
so the same gun bought for it has nothing to toggle. The mark appears in every
mode that opens a shop. While you are in that list, the preview box also names
the key that cycles the equipped gun's mode.

The Endless outpost stocks both weapon lists from one pool of guns, so either
list can offer a gun the campaign only ever sold for the other bay. Those rows
are marked **Rear** in the front weapon list and **Front** in the rear weapon
list. Any gun in the list can be bought and fired from that bay; one built for
the other bay often behaves differently there.

## Arcade tweaks

Open **Setup > Enhancements > Gameplay > Arcade Modes**. All three settings are
on under the Engaged preset and off under Vanilla; the host controls them online.

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

LAN discovery works on the same subnet and finds hosts whatever Listen Port
they picked. Direct join addresses are remembered. The game refuses online play
when the FPS cap is below 35.

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
  row **Host Fights On**. It applies to a new game; loading a save gives the
  host the side it saved with instead.
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

Rollback moves your ship at the display rate. Delay-Based draws it at the
delayed simulation position. The remote ship is smoothed between received
positions in either mode, so it may drift and correct.

Ship movement is part of the simulation, so a rollback session uses the host's
**Smooth Motion** setting. A host who plays with it off gives the whole session
fixed-step ship movement.

Online games do not pause. Pressing P or changing window focus leaves the game
running.

Esc opens the in-game menu on its input frame. Rollback may replay a fraction of
a second to reach that frame, which can undo a very recent hit. If both players
press Esc on the same frame, the host gets the menu.

Pressing **Start Level** waits for the other player. Esc takes that wait back and
returns you to the menu, so you can use Options or keep outfitting while they
finish. Esc works until both players have committed; after that the level loads.
If the other player withdraws after you committed, your machine returns to
waiting instead of leaving without them.

### Game types

**Linked Arcade** uses the Silver Ship and Dragonwing with the split HUD and
docking rules.

**Separate Arcade** gives both players a complete Stalker with their own lives,
weapons, sidekicks, special, score, and HUD.

**Campaign** gives both players complete ships and separate shops. The host's
planet wins when the two players choose different routes.

The **2 Player Campaign** high-score page keeps one record per episode. It stores
both names, the combined wallets, and the Credit rule.

The record is written only when the pair finishes its starting episode. Later
episodes, loops, and deaths do not replace it.

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
wait, and can return to the outpost with Esc until departure is committed. Under
Alternating the turn passes only once a sector has been flown to its end, so a
sector the pair dies on or quits is re-charted by the same player.

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
shows both totals, and either player can dismiss it. Timed Battle cannot load a
save and does not write to the single-player Timed Battle boards.

### Telling the two ships apart

During an online game, choose **Customize** at the outpost.

- **Ship Color** dyes your hull and sidekicks. Your partner sees it too, while
  shots keep their weapon colors. Shades follow the current level palette;
  Endless hides the four colors used by kill-fire drives.
- **Partner Opacity** fades the other player and their shots from 100% to 20%.
  With **Apply to Ship** on, their hull, shield-hit effect, and HP bars fade too.
  Turn it off to fade only their shots. This setting affects only your screen.
- **Partner HP Bars** displays the other player's shield and armor, either always
  or for two seconds after a hit. Placement and base opacity use the enemy-bar
  settings under **Setup > Enhancements > Heads-Up Display**. With **Apply to
  Ship** on, the bars fade with the ship.

Linked Arcade omits Partner HP Bars because its shared HUD already shows both
players.

**Partner Opacity** and **Partner HP Bars** affect only your screen. Online saves
keep both players' choices, even if the other player hosts the resume.

### Saving and resuming

Save from the shop with **Options > Save Game** or Alt+S. A save writes only to
the machine it is made on, and it captures both ships' outposts: the other
machine confirms its half over the connection first. Save on your own machine
too if you want to host the resume yourself.

If the other player is still on the level end screen, the save shows
**Waiting for other player.** until they reach the outpost. Press Esc to write
the save without waiting. A save made from the disconnect prompt skips the
confirmation; there is no one left to ask.

Linked Arcade saves remain compatible with local two-player play. Separate
Arcade, SuperTyrian, Super Arcade, Campaign, and Endless saves must be loaded
through the same online game type that wrote them.

The automatic `LAST LEVEL` slot is written at level start. After a disconnect,
the game can offer a save based on the outpost before the interrupted level.
Hardcore Endless never offers a save.

To resume, host the matching game type and choose **Load Game** after the joiner
connects. Loading is available only at session start.

Campaign and Endless resumes open at the outpost. Endless restores each
player's confirmed stock and rerolls. If the peer confirmation never arrived,
the joining player's stock is dealt again.

Everyone keeps the player number they saved with. A second player who saves
after the first one disconnects is still player two on the resume, even when
they host it, and the returning player is still player one. In Linked Arcade
that player number is the ship, so the saved side comes back too.

### Desync reports

Attach the matching network log from both machines. Each log contains its local
state for the disputed frame.

Network logging is under **Setup > Diagnostics**. Console builds also provide
**Clear Logs**.

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
| Weapons | Weapon Creator, Charge-Laser, autofire, spark trails |
| Gameplay | Shot hitboxes, Guided Aim, Ice Base Shots, Arcade tweaks |
| Episode Versions | Items whose Ep 1-3 and Ep 4-5 data differ |

### Presets

The **Preset** row at the top sets every enhancement at once:

| Preset | Effect |
| --- | --- |
| Vanilla | Plays as close to the original Tyrian 2000 as the settings allow |
| Engaged | The recommended set, which is also what a new install starts with |
| Custom | The last set you built by hand |

Changing any setting makes the current values your Custom set. Switching to
Vanilla or Engaged and back to Custom returns them, and the set survives a
restart. Editing again from either preset replaces the remembered set with the
new values, so only the most recent one is kept. Custom stays grayed out until
you have changed something for it to hold.

Two settings the Enhancements menu shows are outside the presets. **Sidekick
Autofire** is stored per save slot, so loading a game sets it and no preset
changes it. **Weapon Creator** designs weapons rather than holding a setting.

Episode Versions rows accept Auto, Ep 1-3, or Ep 4+. Auto follows the data for
the current episode. Online sessions use the host's choices. A changed row takes
effect immediately, including in a game already in progress.

Vanilla sets every Episode Versions row to Auto. Engaged uses:

- the Ep 1-3 Xega Ball with two weaker balls;
- Ep 1-3 sound for the Bubble Gum-Gun;
- Ep 4+ sounds for the other four weapons;
- the Ep 4+ Solar Shield icon;
- Ep 1-3 shop pictures for the two borrowed ship illustrations.

Changing a **Firing Sounds** row previews the selected sound.

**Episode Versions > Shop Pictures** holds the three items that differ only in
artwork. The U-Ship and the Nort Ship have no shop illustration of their own, so
the two data sets each lend them a different ship's; their rows pick which.

Useful restored-content settings:

- **Ice Base Shots** enables the dormant dispenser-base attack. Endless rolls it
  from the run seed and always enables it from zone 50.
- **Unused Sprites** assigns distinct shipped icons to weapons that otherwise
  share or lack one. It also rebuilds eleven duplicated special icons from each
  ship body and weapon sprite. Dragon Lightning gets its own spare icon.
- **Special Tint** controls the full-screen wash from flare-family specials. It
  is visual and may differ between online players.
- **Vulnerable Cue** greys invulnerable boss bars and flashes enemies when they
  become damageable. Bosses is the default; All also flashes regular enemies.
  It is visual and may differ between online players.
- **Shot Hitboxes** selects Classic top-left anchors or Centered sprite anchors.
  The host controls it online.
- **Guided Aim** makes homing weapons such as the Heavy Guided Bombs steer to
  the drawn enemy instead of a map coordinate displaced by parallax. It is off
  in both presets. The host controls it online.

The special-weapon HUD light is a charge meter. It drains during use, fills
during recharge, and flashes when ready.

## Diagnostics

**Setup > Diagnostics** holds Debug Mode and the network log. These configure
tools rather than the game, so the Enhancements presets leave them alone.
Console builds also provide **Clear Logs**, which deletes every stored log.

### Debug Mode

Debug Mode is off by default. It adds a menu to the shop and in-game menu, plus a
level browser. It can change loadout, cash, cheats, difficulty, and expert
multipliers.

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
| FluidSynth | Windows x86-64 with a `.sf2`, `.sf3`, or `.sf` SoundFont |
| Native MIDI | Windows x86-64 |

Choose a backend under **Setup > Sound > Music Synth**. Put a SoundFont beside
the executable or in `data`; FluidSynth is unavailable in the menu until one is
found.

## Console and mobile builds

Switch, Vita, Android, and iOS builds include online play and use the system
keyboard for text fields. MIDI is disabled.

- [Nintendo Switch](switch/README.md)
- [PlayStation Vita](vita/README.md)
- [Android](android/README.md)
- [iOS](ios/README.md)

### Touch controls

Every touch build steers the same way: drag anywhere on the screen and the ship
follows your finger, no matter where the drag started. Holding a finger down
also holds the main weapon down, so there is no separate fire control. **Setup >
Sensitivity** scales how far the ship travels per finger movement; the middle of
the slider tracks your finger one to one.

Phones and tablets add on-screen buttons for everything a finger cannot express
on its own. Which buttons appear depends on the screen, so nothing is on display
that would do nothing if you pressed it.

| Screen | Left side | Right side |
| --- | --- | --- |
| Flying a level | Pause, left sidekick | Rear weapon mode, both sidekicks, right sidekick |
| Any menu | Back | None |
| A list too long to fit | Back, left, right | Up, down, confirm |
| Weapon Creator | Back, left, right | Up, down, confirm |
| Destruct mode select | Back | Up, down, confirm |
| A screen waiting for any key | Back | Confirm |
| Jukebox | Back, previous track | Next track |
| Rear Gun in the shop | Back | Rear weapon mode |
| Destruct | Back, aim left, aim right, change unit | Next weapon, more power, less power, fire |

Back is always the top left button and always does what Esc does on a keyboard,
so no screen can trap you.

In an ordinary menu that is the only button, because a tap is already a click:
menu rows, sliders and pickers all follow your finger. Cursor keys only appear
where tapping cannot do the job. That means a list longer than its frame, where a
tap only reaches the rows currently drawn: the debug screens, and the shop's buy
and sell list when the outpost is carrying more than six items. The
Weapon Creator has them for a related reason: it does hit-test every row, but a
tap there both moves the cursor and acts on it, so the arrows are the only way to
line a field up before changing it. Destruct's mode
select gets up, down and confirm for a different reason. It reads the keyboard
directly and hit-tests nothing, so a tap there does nothing at all, and its
title, help and pause screens get confirm for the same reason. Holding an arrow
repeats it.

Destruct holds its aim, power, and fire buttons the way a key would, and steps
the unit and weapon one press at a time.

Rows line up across the two sides: left sits level with up, right with down, and
whatever that screen treats as its main action sits on the bottom row.

The three sidekick buttons are the exception to "only what the screen needs":
they are the only way to fire a sidekick by hand on a touch device, but nothing
requires them, so **Setup > Enhancements > Heads-Up Display > Sidekick Buttons**
turns them off. They are on by default, and the setting is not part of the
Enhancements presets: a preset describes how the game behaves, not which
controls you are given. Each icon shows both sidekick slots, with the ones that
button fires drawn firing.

The buttons sit outside the playfield. A phone is wider than the 16:9 frame the
game draws, so they take the pillarbox beside it, against the frame edge rather
than the screen edge; that keeps them clear of a display cutout in landscape. A
4:3 tablet has no pillarbox but does have a band above the frame, and they use
that. On a display at exactly 16:9 there is no margin at all, and they fade back
over the frame edges instead.

A finger held on a button never steers and never fires, so the other thumb can
keep flying.

Switch and Vita have physical buttons and draw none of this.

## Files and logs

On Windows, files sit beside the executable. Linux uses
`~/.config/opentyrian2000`, and the macOS app bundle uses
`~/Library/Application Support/OpenTyrian/OpenTyrian2000`. Console and mobile
locations are listed in their build guides; on Android and iOS the files live in
app-private storage that the system deletes on uninstall.

| File | Contents |
| --- | --- |
| `opentyrian.cfg` | Settings and records |
| `opentyrian.sav` | Save slots (campaign, two-player and Endless) and high scores |
| `log/opentyrian_log_<time>.log` | Windows crash report |
| `log/opentyrian_net_<time>.log` | Online session log |

`opentyrian.cfg` and `opentyrian.sav` are plain text. Edit them only while the
game is closed.

`opentyrian.sav` contains:

- `section 'save' 'N'` for each occupied slot;
- `section 'endless' 'N'` for an Endless run;
- `section 'highscore'` for each board.

Slots 1 through 11 are the one-player page; 12 through 22 are the two-player
page. Missing values use defaults, unknown keys are ignored, and cash is clamped
to 0 through 999,999,999,999. Removing a slot's sections empties that slot.

Older builds kept `tyrian.sav`, `endless.sav` and `tyrian.cfg` instead. The first
launch that finds no `opentyrian.sav` imports all three and leaves them in place.
After that, `endless.sav` is read only to restore a slot named `ZONE n` whose
`endless` section is missing; such a slot replays one level instead of resuming
the run. Keep it beside the game until every run has come across.

Logs are created on first use and are not rotated automatically.
