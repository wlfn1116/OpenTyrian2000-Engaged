# OpenTyrian2000 Engaged player guide

This guide covers the features added by Engaged. The original Tyrian campaigns
and controls work as before.

## Menu map

| Task | Menu |
| --- | --- |
| Start Endless | Main menu > 1 Player Endless |
| Play online | Main menu > Online Multiplayer |
| Enable smooth rendering | Setup > Graphics |
| Play as close to the original as possible | Setup > Enhancements > Preset |
| Design a weapon | Setup > Enhancements > Weapons > Weapon Creator |
| Change health bars or gauges | Setup > Enhancements > Heads-Up Display |
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

On Normal, most ordinary scaling approaches its cap between zones 55 and 100. A
rising tide begins near zone 25 and adds shots, then damage from zone 30. The
share of elites and champions climbs for far longer: one enemy in fifty at the
start, one in four around zone 40, three in five by zone 100, and four in five
from zone 200 on. Champions are the scarcer tier within that share, one special
in ten at the start, under a third at zone 100, and seven in ten by zone 200. A
harder difficulty reaches every mark sooner in proportion to its ramp: Hard
takes the zone-100 mix by zone 83 and the ceiling by zone 167, Suicide by zones
63 and 125.

| Tier | Health | Offense | Bounty |
| --- | --- | --- | ---: |
| Normal | Depth-scaled | Normal | None |
| Elite | 2x to 4x | +25% contact damage | $150 at zone 1, +$40 per zone, max $2,500 |
| Champion | 2x to 4x | Faster fire, +50% shot and contact damage | $600 at zone 1, +$170 per zone, max $11,000 |

Piercing shots follow their own damage rule. The Mega Cannon, Sonic Impulse, and
Needle Laser fire them at every power level, and Dragon Frost and Dragon Flame
from level 9 up. A piercing shot is never used up, so it damages every hull it
crosses and hits again on each tick it stays over a target. It also carries only
one or two points of damage, and some of these weapons mix in shots that carry
none at all. Depth raises piercing damage on a curve of its own, and your damage
perks and drives apply on top of that. Bosses, elites, and champions ignore
repeat hits from the same shot for a fraction of a tick, which limits how fast
one shot can wear a single target down.

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

The Base Level rule is fixed when the run starts. Each of the four rules keeps
separate records.

### Outpost and E-Shop

The ordinary shop restocks at each visit. Guns that ship under one name are
shown apart in its menus: the front and rear Protron as Front Protron and Rear
Protron, the two Multi-Cannons as Front Multi-Cannon and Rear Multi-Cannon, and
the twin-barrel Vulcan Cannon as Twin Vulcan Cannon; the sweeping-fire Vulcan
Cannon keeps the original name. Data Cubes opens the E-Shop and Ship Specs
opens the perk list.

The ship list can offer the Dragonwing, the wide hull player two flies in the
linked two-player pair. No campaign shop sells it. Here it flies as a full
ship with its own front and rear guns, priced and armoured between the
Gencores and the MicroCorp Stalkers, and the sidekicks that trail your ship sit
wider apart to clear its wings. Three hulls are renamed after their Super Arcade
counterparts: the Nort Ship as the Nort Ship Z, the Silver Ship as the TX
SilverCloud, and the PeteZoomer as the Pretzel Pete Truck.

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
| Countermeasures | Clears nearby shots after damage | 2 |
| Chain Reaction | Kills blast nearby enemies, and those blast on | 3 |
| Financier | More interest and lower shop prices | 4 |
| Ordnance Reserves | More sidekick ammo and special duration | 4 |
| Failsafe | Brief invulnerability after hull damage | 2 |
| Guidance Package | Shots home in; later stacks add sidekicks and specials | 3 |
| Twin Pods | Every sidekick volley fires twice, at double ammo and power | 1 |
| Reinforced Prow | Ramming deals 2x, 3x, 4x and costs you 75%, 50%, 25% | 3 |
| Knife Fight | +15% damage within 7 px of an enemy, fading out by 55 px | 4 |
| Deflector | A shot your shield absorbs flies back as yours; 2x damage at two stacks | 2 |

A few perks work in ways the table cannot show.

**Guidance Package** bends shots toward the nearest enemy that can be hurt: the
main guns from one stack, sidekicks from two, specials from three, and each
stack corrects more often. A slow or wide gun curls onto its target while a fast
one barely bends until full stacks. A gun that already homes turns quicker
instead of gaining a second aim, and a steered shot moves to the next enemy when
its own dies. Shots that ride the ship, such as the Zica Laser beams, curve
inside its frame. Score pickups, scenery, invulnerable parts, and superbombs are
never targets.

**Twin Pods** adds a second volley beside each sidekick's own, the two leaving a
few pixels apart and centred on the pod. The twin is a full shot: it draws
generator power and spends a round of a limited magazine, so a pod on its last
round fires alone and a pod refused for power fires nothing. Charge sidekicks
fire the twin at the same charge, and the shop preview shows the pair.

**Reinforced Prow** makes ramming a way to fight. Contact normally costs each
side 2 per tick; every stack multiplies the enemy's share (2x, 3x, 4x) and cuts
yours (75%, 50%, 25%), never below one point. An enemy you destroy by ramming
pays its cash, drops, and bounty exactly as a shot one does, feeds your kill
streak, and starts a Chain Reaction wave. While you are invulnerable you ram
without being rammed back, but land one contact hit every ten ticks instead of
one every tick. Bosses, elites, and champions spend ram damage through the same
health multiplier your guns face.

**Knife Fight** measures the gap between your hull and the nearest edge of the
enemy you hit, counting the nearest tile of a body built from several, so
hugging one end of a boss is enough. Within 7 px every stack adds 15%, and the
bonus fades evenly to nothing at 55 px. It applies per enemy hit to shots,
specials, and rams, and never to Chain Reaction blasts. A raised hit bleeds,
more heavily the deeper the bonus, so you can read the range without looking
away from the fight.

**Deflector** returns a shot the shield absorbs whole. It flies back along the
reverse of its path as your own shot, carrying the damage the shield absorbed,
doubled at two stacks and then scaled like anything else you fire. It takes no
steering, so a shot fired from ahead goes straight back at the enemy that fired
it, and it keeps the bullet's look. A deflection made inside an Opening Salvo
window belongs to that volley. A hit that reaches the hull, a hit taken while
you are invulnerable, and an empty shield all return nothing, and Bulwark's cut
comes off before the shield absorbs, lowering what comes back.

**Opening Salvo** charges after two seconds without main-gun fire. Its
one-second window gives 2.5x damage, removes generator cost, and covers
everything you fire inside it, sidekicks and the special included. The generator
gauge turns green when the volley is ready. A special that fires no shot, such
as a repulsor, a repair, or an invulnerability, gets the 2.5x on its effect
instead.

**Kinetic Converter** pays out on every hit that costs shield or hull: 8% per
stack off whatever is left of the special recharge, 0.25 sidekick rounds per
stack, and one charge stage per stack on a charge sidekick, with part-rounds
carried over to later hits. Shield absorption also refunds 20% per stack of the
generator power that shield charge was worth. Separately, it cuts 22% per stack
from the shield or armour charge of a twiddle (a special fired by a movement
code), never below one point, and the twiddle still delivers what its full price
buys.

**Chain Reaction** blasts nearby enemies on every kill, and each stack past the
first deepens the damage and widens the blast. The blast is your damage: Heavy
Rounds, Glass Cannon, Adrenaline, a drive, and an Opening Salvo volley all
deepen it, anything cutting your damage cuts it too, and a wave keeps the salvo
bonus for its whole cascade.

Anything the blast destroys blasts in turn, one hop per tick, so a kill in a
packed formation sends a visible wave through it, while an enemy the blast only
wounds stops the wave there. A wave lands on each enemy once, and on a hull
built from several tiles once rather than once per tile, so a wall of tough
tiles loses one layer per kill. Bosses, elites, and champions take the blast
through their own health multiplier. Kills the wave makes pay exactly what
shooting those enemies would have. Anything that fires sets the perk off,
ramming and scattered special fire included.

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

Data cubes and secret-level orbs become random safe specials. Both draw as a
colour-cycling "?" that throws matching sparks, and collect only on the mark
itself. An armored orb keeps its own look until you shoot it open. A
random-special event becomes a weapon power-up. Two of the specials in that pool
are named Pearl Wind; the one that fires a single aimed bolt is shown as Pearl
Shot, and the flare-style field keeps the original name.

### Death and saving

A revive restores the hull, clears enemy shots, and briefly stops enemy fire.
Without one, Relaxed offers Restart Zone, Return to Outpost, or End Run.

Relaxed and Standard checkpoint at the outpost, into the same save slot as the
campaign half of the run (`opentyrian.sav`, see Files and logs). Hardcore never
writes a run save. Quit Level restores the launch snapshot. Such a slot reads
**Endless** on the Saved Games screen and `End` in the outpost's **Load Game**
and **Save Game** list, where a campaign save names its episode.

Records are split by run mode, difficulty, Base Level rule, and crew size. A
trailing `C` marks a record set while either ship used a custom weapon in a zone.
The **Endless** page under **High Scores** opens on the furthest zone each mode
has reached under any rule; selecting a mode lists the four Base Level rules
behind it, and selecting a rule breaks that down by difficulty. Records are
erased on that last list, behind a confirmation.

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

Rollback also moves your own ship at the display rate, the same as offline play,
so it answers the controls without waiting for the network. Delay-Based draws
your ship at the simulation position, which trails your input by the configured
delay. In both modes the other player's ship is placed from the positions
arriving over the network and smoothed between them, so it can drift and correct
in a way your own ship never does.

Ship movement is part of the simulation, so a rollback session uses the host's
**Smooth Motion** setting. A host who plays with it off gives the whole session
fixed-step ship movement.

Online games do not pause. Pressing P or changing window focus leaves the game
running. Use Esc for the in-game menu.

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

The **2 Player Campaign** page under **High Scores** keeps one record per
episode: the two wallets added together when the pair finished the episode they
started, under both names. A run that has carried on into a later episode or
looped back to the first records nothing, and neither does dying, since a
campaign death reloads the save from the start of the level. Each record names
the **Credit** rule it was earned on, because Shared and Double Earnings both
pay roughly twice what a plain Individual split does.

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
shows both totals, and either player can dismiss it. Timed Battle cannot load a
save and does not write to the single-player Timed Battle boards.

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
connects. Loading is available only at session start. A resumed Campaign or
Endless session opens the outpost for both players before its level. An Endless
save hands each player back their own shop stock, a reroll bought before saving
included. A save whose confirmation never arrived, after a disconnect for
example, deals the joining player's stock again instead.

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

Vanilla sets every one of these rows to Auto. Engaged keeps Auto for the three
gameplay reworks and pins the rest per item, chosen for how each sounds or looks
rather than by era: the Bubble Gum-Gun keeps its Ep 1-3 sound and the other four
weapons take their Ep 4+ one, the Solar Shield takes its Ep 4+ icon, and the two
borrowed ship pictures stay on Ep 1-3. **Firing Sounds** rows play the sound they
land on as you change them, so the two versions can be compared without starting
a game.

**Episode Versions > Shop Pictures** holds the three items that differ only in
artwork. The U-Ship and the Nort Ship have no shop illustration of their own, so
the two data sets each lend them a different ship's; their rows pick which.

Useful restored-content settings:

- **Ice Base Shots** enables the dormant dispenser-base attack. Endless rolls it
  from the run seed and always enables it from zone 50.
- **Unused Sprites** assigns distinct shipped icons to weapons that otherwise
  share or lack one, and rebuilds the HUD icons of eleven specials that shared
  one: each keeps its ship body and gains the weapon's own sprite above it.
  Dragon Lightning takes a spare icon of its own, leaving the bolt it shared to
  Lightning Zone.
- **Special Tint** controls the full-screen wash from flare-family specials. It
  is visual and may differ between online players.
- **Shot Hitboxes** selects Classic top-left anchors or Centered sprite anchors.
  The host controls it online.
- **Guided Aim** makes homing weapons such as the Heavy Guided Bombs steer to
  where an enemy is drawn. As shipped they steer to its position on the map,
  which sits to one side of the sprite by the parallax of the enemy's layer, so
  they can miss what they chase. Off by default in both presets, so Vanilla and
  Engaged keep the shipped homing. The host controls it online.

The special-weapon HUD light is a charge meter. It drains during use, fills
during recharge, and flashes when ready.

## Diagnostics

**Setup > Diagnostics** holds Debug Mode and the network log. These configure
tools rather than the game, so the Enhancements presets leave them alone.
Console builds also provide **Clear Logs**, which deletes every stored log.

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
| `opentyrian.sav` | Save slots (campaign, two-player and Endless) and high scores |
| `log/opentyrian_log_<time>.log` | Windows crash report |
| `log/opentyrian_net_<time>.log` | Online session log |

`opentyrian.cfg` and `opentyrian.sav` are plain text in the same format and can
be read and edited with any text editor while the game is closed.
`opentyrian.sav` holds a `section 'save' 'N'` for every slot that has a game in
it (slots 1-11 are the one-player page, 12-22 the two-player page), a
`section 'endless' 'N'` beside it when that slot holds an Endless run, and a
`section 'highscore'` for every board. Every value is a named `item`; a line you
delete goes back to its default, an unknown line is ignored, and cash outside
0 to 999,999,999,999 is clamped, so a mistake costs that one value and never the
file. Deleting a slot's sections empties the slot.

Older builds kept `tyrian.sav`, `endless.sav` and `tyrian.cfg` instead. The first
launch that finds no `opentyrian.sav` imports all three and leaves them in place.
After that, `endless.sav` is read only to restore a slot named `ZONE n` whose
`endless` section is missing; such a slot replays one level instead of resuming
the run. Keep it beside the game until every run has come across.

Logs are created on first use and are not rotated automatically.
