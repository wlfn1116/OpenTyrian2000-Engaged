# OpenTyrian2000 Engaged — Player's Guide

Everything this fork adds, explained. The original story mode, arcade modes and
levels are untouched; nothing here changes them unless you turn it on.

The one chapter you actually need is [Endless mode](#endless-mode). The rest is
reference for the settings menus.

- [Quick start](#quick-start)
- [Endless mode](#endless-mode)
  - [How a run flows](#how-a-run-flows)
  - [Starting a run: seed, hardcore, difficulty](#starting-a-run-seed-hardcore-difficulty)
  - [What gets harder as you descend](#what-gets-harder-as-you-descend)
  - [Elite and champion enemies](#elite-and-champion-enemies)
  - [Sector modifiers](#sector-modifiers)
  - [Chart-a-Course](#chart-a-course)
  - [The outpost](#the-outpost)
  - [The E-Shop](#the-e-shop)
  - [The gamble](#the-gamble)
  - [Perks](#perks)
  - [Money](#money)
  - [Pickups that behave differently](#pickups-that-behave-differently)
  - [Milestone zones](#milestone-zones)
  - [Dying, quitting, saving](#dying-quitting-saving)
  - [Endless effects in a normal campaign](#endless-effects-in-a-normal-campaign)
- [Display and motion settings](#display-and-motion-settings)
- [Enhancements](#enhancements)
- [Weapons](#weapons)
- [Music](#music)
- [Consoles](#consoles)

---

## Quick start

| I want to… | Go to |
| --- | --- |
| Play the new roguelite mode | Main menu → **1 Player Endless** |
| Make the game smooth on a 144 Hz monitor | Setup → Graphics → **Smooth Motion**, **Sub-pixel** |
| Change the boss health bars | Setup → Enhancements → **Boss Health Bars…** |
| Design my own weapon | Setup → Enhancements → **Custom Weapon Creator…** |
| Hear the music as MIDI instead of OPL | Setup → Sound → **Music Synth:** |
| Get the per-episode weapon behaviour back | Setup → Enhancements → **Weapon Tweaks…** |

---

# Endless mode

Endless is a roguelite run built out of Tyrian's own levels. It does **not**
invent new levels or edit existing ones — every zone you fly is a real, shipped
level from one of the episodes, played exactly as authored. What changes is the
*enemies* in it, the *route* you took to get there, and what you brought.

A run ends when you die. There is no final level; the goal is depth.

## How a run flows

```
Outpost (shop)  →  Chart-a-Course  →  fly the zone  →  clear it  →  Outpost  →  …
```

1. **Outpost.** The standard Tyrian shop, plus endless-only rows and a second
   screen called the E-Shop. Spend, upgrade, take a perk if one is owed.
2. **Chart-a-Course.** You are offered 2–5 routes. Each is a real level dressed
   in its own set of modifiers, with a danger rank and a cash payout shown up
   front. Pick one.
3. **Fly it.** The level plays normally. The modifiers you accepted are live.
4. **Clear it.** You are paid the clear bonus plus interest, the zone counter
   ticks up, and you land at the next outpost.

Zone 1 is the first level you fly. "Depth" in this guide means zones cleared.

## Starting a run: seed, hardcore, difficulty

Choosing **1 Player Endless** opens a seed screen with four rows:

- **Seed** — type one, or leave it blank for a random one. A seed fixes the
  *structure* of the run: which levels come up in which order, what music each
  plays, what the courses offer, what the shop stocks, which perks you're shown.
  Same seed + same choices = same run. It does **not** fix moment-to-moment
  combat, so two players on one seed will not have identical fights.
- **Randomize** — rolls a fresh seed.
- **Hardcore: On/Off** — Hardcore disables saving completely. Die or quit and
  the run is gone. Without it, the game checkpoints at every outpost.
- **Start.**

Your seed is shown on the E-Shop help line all run, so you can note down a good one.

**Difficulty** is the normal Tyrian difficulty you pick when starting a game, and
in Endless it does two things: it sets your starting cash, and it tilts the
entire depth ramp. On Hard, zone 60 fights like zone 72 does on Normal.

| Difficulty | Starting cash | Ramp |
| --- | --- | --- |
| Wimp | $45,000 | 50% |
| Easy | $34,000 | 75% |
| Normal | $25,000 | 100% |
| Hard | $18,000 | 120% |
| Impossible | $14,000 | 134% |
| Insanity+ | $9,000 | 160% |

You launch with the **Atomic RailGun** at power 1, not the campaign's Pulse Cannon.

## What gets harder as you descend

Enemy statistics scale with depth. The levels do not change — a zone-80 GYGES has
exactly the same layout as a zone-3 GYGES, but the things in it hit far harder.

Each lever has its own slope, so they mature one at a time instead of arriving as
a single wall. Roughly, on Normal difficulty, each maxes out around:

| Lever | Maxes out near zone |
| --- | --- |
| Enemy shot damage | 55 |
| Elite/champion hull | 64 |
| Enemy shot speed | 67 |
| Enemy fire rate | 80 |
| Boss hull | 96 |
| Ordinary enemy hull | 100 |
| Share of enemies that are elite | 118 |

Once those saturate, two things keep climbing with no ceiling:

- **The rising tide** (from about zone 28) adds *extra shots per volley* and
  keeps pushing the elite/champion share up. Extra shots: +1 at zone 25, +3 by
  zone 100, then +1 more every 25 zones forever. Shot damage resumes climbing
  too — roughly +30% by zone 100, +70% by zone 200.
- **Contact damage** — the damage *you* take from ramming things. No bonus until
  zone 35, then up to +150% by zone 100 and onward to a +500% cap around zone
  252. Enemies take the same ram damage from you as always; only your side scales.

Course danger also tilts against you from about zone 40 — the slate you're offered
skews toward nastier sectors the deeper you go.

## Elite and champion enemies

A share of the enemies in every sector spawn as a **special tier**. There are
three tiers:

| Tier | Look | Hull | Guns | Ram | Bounty |
| --- | --- | --- | --- | --- | --- |
| **Normal** | unchanged | stock (depth-scaled) | stock | stock | none |
| **Elite** | recoloured, cool-tinted aura | ×2 rising to ×4 | *no gun bonus* | +25% | $150 + $40/zone (cap $2,500) |
| **Champion** | recoloured, purple aura | ×2 rising to ×4 | fires ~1.7× as fast, +50% shot damage | +50% | $350 + $90/zone (cap $6,000) |

Points worth knowing:

- **The tint is the tell.** Elites and champions are palette-shifted, and their
  little enemy health bar (if you have those turned on) is drawn in the same
  colour as the aura. The exact hue depends on the level's own palette.
- **An elite is a tougher hull, not a tougher gun.** Only champions shoot
  harder and faster. If something is shredding you, it's a champion.
- **Multi-part enemies decide once.** Every tile of a linked enemy is the same
  tier, and the bounty pays once for the whole enemy, not once per piece.
- **A boss can be elite or champion too.** Its hull multiplier gets a gentler
  ×2 bump on top of the boss ramp rather than the full elite ramp.
- **The share of specials** starts at 2% and climbs with depth: 25% at about zone
  37 on Normal, then more slowly to an 80% cap around zone 118. Among specials,
  champions start at about 1 in 3 and rise toward 70% deep in a run.
- Several sector modifiers override the share outright — Elite Pack forces 50%,
  Apex forces 100%, Legion forces 100% *champions*. Two boons go the other way:
  No Champions downgrades every champion to an elite, No Elites removes the tier
  entirely. Those two boons only start appearing once the natural share has
  passed 25%, because before that they'd barely do anything.
- **Bounties are cash, not score**, and they're paid on top of the enemy's normal
  point value. Bounty Hunter doubles them; Scavenger multiplies them again.

Two boons change what a special tier *means* rather than removing it:

- **Giant Killer** takes the hulls: elites and champions keep their tint,
  aggression and bounty, but have ordinary armour.
- **Clean Signals** takes the guns: they keep hull, tint and bounty, but their
  fire-rate, shot-damage and ram premiums all go back to neutral.

## Sector modifiers

Every charted sector carries a set of modifiers. They are shown on the
Chart-a-Course monitor: **threats in red at the top-left, boons in green at the
bottom-right**, each as a plain phrase describing what it does.

Hostile modifiers raise the sector's danger rank **and its payout**. Boons lower
both — an easier sector genuinely pays less. That is the core trade of the mode:
*taking the harder route is how you get paid.*

The "pays" column below is the bonus as a fraction of the base clear reward, so
`+1.0` means the sector pays double base for that bit alone; negatives pay less.

### Threats

| Modifier | What it does | Pays |
| --- | --- | --- |
| Fortified | More enemy HP | +1.0 |
| Frenzy | Enemies fire much faster | +1.0 |
| Swift | Much faster enemy projectiles | +0.8 |
| Devastating | Enemy shots deal much more damage | +1.0 |
| Enrage | Enemy fire rate climbs the longer the zone runs | +1.0 |
| Gravity Well | A constant pull drags your ship down. Half of all gravity sectors are instead **Rogue Wells**, pulling along a fixed random heading for that sector | +0.8 |
| Elite Pack | Half of all enemies are elite/champion | +2.0 |
| Apex | *Every* enemy is elite/champion | +4.0 |
| Legion | Every enemy is a **champion** | +5.0 |
| Overclock | Faster enemy fire **and** shots **and** scrolling | +1.6 |
| Overload | Overclock cranked way up | +3.0 |
| Slipstream | The level scrolls at you faster — less reaction time | +0.6 |
| Warp | Slipstream cranked way up | +2.0 |
| Light homing | Enemies barely lean toward you | +0.6 |
| Kamikaze | Enemies home in at a moderate pace | +1.2 |
| Rampage | Strong homing **and** extra ram damage | +5.0 |
| Topsy Turvy | The playfield flips upside-down; controls invert with the view | +1.0 |
| Molasses | Your ship crawls — keyboard, mouse and touch all slowed | +1.5 |
| Shieldless | Shields never recharge; once spent you fly on armour | +1.2 |
| Dead Generator | No shield regen **and** the main gun is starved of power | +3.0 |
| Martyrdom | A destroyed enemy fires a final radial burst (4 shots normal / 6 elite / 8 champion) | +1.8 |
| Seeker | Each enemy shot makes one course correction toward you about half a second after firing | +1.4 |
| Static | Taking damage bleeds generator power and briefly shorts out the regen | +1.1 |
| Retaliation | Every kill you make briefly quickens enemy fire (~25%) | +1.5 |
| Backfire | Each kill briefly jams your guns | +1.2 |
| Burnout | The jam, plus each kill stacks a fire and damage penalty | +1.8 |
| Misfire | Each kill stacks a shot-damage cut | +1.4 |
| Overheat | Kills quicken your guns, but the hull cooks (chip damage over time) | +1.4 |
| Marked | The next sector's boss is beefed up | — |
| Nitro | Your shots hit harder, but *any* hit kills you | — |
| Dud | Your superbombs refuse to fire this sector | — |

Enrage and Retaliation are easy to confuse: **Enrage is on a clock** (it ramps as
the zone runs), **Retaliation is on your trigger** (it spikes when you kill).

### Boons

| Modifier | What it does | Pays |
| --- | --- | --- |
| Fragile | Enemies have much less HP | −0.5 |
| Bounty | Big clear payout, no added danger | +3.0 |
| Cursed | A fortune in cash now, but the next shop is barren | +4.0 |
| Turbodrive | Each kill briefly quickens your guns | 0 |
| Overcharge | Your weapons hit much harder | 0 |
| Overdrive | Turbodrive + Overblast together — quickened guns *and* stacking shot damage | 0 |
| Overblast | Overdrive's damage half only — kills stack shot damage | 0 |
| Dilation | Enemy shots move much slower | 0 |
| Merchant Favor | The next outpost slashes its prices | 0 |
| No Champions | Champions downgrade to elites | 0 |
| No Elites | No elites or champions at all | 0 |
| Aegis Gate | While the shield holds, a hit can't spill into armour — the gate empties the shield instead | −0.5 |
| Flak Screen | Halves the *extra* shots the rising tide adds per volley (the level's own fire is untouched) | −0.5 |
| Auxiliary Reactor | Shield recharge costs no generator power this sector | 0 |
| Low Profile | Your damage hitbox shrinks about 25% (sprite and pickup reach unchanged) | −0.8 |
| Giant Killer | Elites/champions lose their HP multiplier | −0.6 |
| Shockwave | Killing an elite/champion vaporises nearby enemy shots | −0.4 |
| Star Charts | Clearing this sector guarantees a full route slate at the next ordinary outpost | 0 |
| Breakthrough | Clearing this sector owes you a **bonus perk pick** | −1.0 |
| Soft Landing | Ram damage you take is cut to 30% | −0.4 |
| Clean Signals | Elites/champions lose their fire-rate and shot-damage bonuses | −0.5 |

Flak Screen is only offered once the tide is actually running — before that it
would have nothing to halve.

### Danger ranks

The tier word and the letter grade come off the same score, summed from the
modifiers on the sector plus the level's own baseline. The grade is tinted
green-to-red on the monitor.

| Grade | Word | Score |
| --- | --- | --- |
| F | Calm / Boon | 0 |
| E | Low | 1–9 |
| D | Moderate | 10–13 |
| C | Tough | 14–19 |
| B | High | 20–26 |
| A | Severe | 27–33 |
| S | Deadly | 34–39 |
| S+ | Extreme | 40–49 |
| S++ | NIGHTMARE | 50–59 |
| S+++ | APOCALYPSE | 60+ |
| END | FINALITY | the zone-100 finale |

A **Boon** rank means no combat danger at all. A Cursed sector also reads as Boon
— its catch is economic, and it's spelled out in its own red modifier row rather
than in the rank.

## Chart-a-Course

The route screen offers **2 to 5 sectors**, sorted left-to-right from safest to
most dangerous; sectors sharing a letter grade are then ordered by the cash they
pay, cheapest first, so the richer of two equal-risk routes always sits further
right. A Calm sector, when one is offered, is always the leftmost card. Each card
shows:

- the sector's generated name (a theme name drawn from the modifiers it carries),
- its danger tier word and letter grade,
- its modifier rows — threats red, boons green,
- the cash it pays on clear.

Two perks change this screen: **Radar** reveals which shipped level is behind
each card, and **Surveyor** adds an extra route to the slate.

The payout shown is exactly what you'll bank, including anything you bought at
the outpost this visit and any Sabotage charges you've queued. Buying a Sabotage
charge visibly drops every card's payout, because it's buying you an easier
sector — and easier pays less.

The game will not hand you the same level twice in quick succession: the last
five zones are tracked and avoided.

## The outpost

The outpost is the normal Tyrian shop with endless additions. Two of the stock
front-menu rows are replaced, since Endless has no use for either:

| Stock row | Becomes |
| --- | --- |
| Data Cubes | **E-Shop** — the endless-only purchase screen (below) |
| Ship Specs | **Perks** — a read-only list of the perks you're carrying |

Weapons, sidekicks, shields and generators are stocked at random each visit, with
no depth or price preference — anything can show up at any depth.

## The E-Shop

A second screen of endless-only purchases, colour-coded by category. Everything
here applies to the **next sector**, or is a held item.

| Row | Cost | Effect |
| --- | --- | --- |
| **Buy Shop Reroll** | $6,000 + $1,000/zone, then ×1.6 + $3,000 per further reroll this visit | Reshuffles the entire shop inventory. Keeps whatever you have equipped, and sinks "None" to the bottom of each list. |
| **Buy Sector Sabotage** | $25,000 + $2,500/zone, doubling per buy | Strips the worst threat off the sector you chart next. Up to 3 charges can be queued; the threats they'll remove are drawn white on the course cards. |
| **Buy Reinforce** | $15,000 + $2,000/zone, then ×1.5 + $5,000 per further tier this visit | +6 permanent max armour. The cap on total hull bonus starts at 60 and unlocks another +6 step every 6 zones, up to 150. |
| **Buy Extra Perk** | $70,000 + $2,500/zone, doubling per buy, **plus +40% for every perk stack you already own** (capped at ×11) | An extra perk pick on top of the free ones. Deliberately brutal. |
| **Buy Special Weapon** | fraction of the cash you walked in with | Grants a random special weapon, equipped instantly. Never Invulnerability, and never the one you already have. |
| **Buy Turbodrive** | fraction of walk-in cash | Next sector: kills briefly quicken your guns. |
| **Buy Overblast** | fraction of walk-in cash | Next sector: kills stack shot damage (no fire boost). |
| **Buy Overdrive** | fraction of walk-in cash | Next sector: Turbodrive + Overblast together. |
| **Buy Revive** | $150,000 + $10,000/zone, **doubling per revive already spent** | A held token. Survive one lethal hit with a full hull restore, a cleared bullet field and ~3s of stunned enemy guns. |
| **Buy Bomb** | $2,500 + $400/zone, ×1.5 per restock | Superbombs. |
| **Buy Gamble** | $25,000 + $2,000/zone | See below. |

Notes on the three kill-fire buffs:

- **Only one per visit**, and buying any of them locks all three until the run
  reaches a recharge depth — 2 sectors early on, 3 by about zone 70, 4 by zone 90.
  A cash-rich late run cannot buy one every sector.
- **The price is a fraction of the cash you walked in with**, frozen on entry.
  Spending inside the shop doesn't change it, and buying one doesn't cheapen the next.
- **How much you pay is how strong it is.** The window length and damage scale
  with the cash spent, normalised by depth.
- While a kill-fire buff is running, your hull is tinted (red Turbodrive,
  yellow Overdrive, blue Overblast) and the HUD shows the live combo count,
  timer and current fire/damage bonus.

Each of the three has an evil twin that can arrive from a bad gamble or a hostile
sector: **Backfire** (kills jam your guns), **Burnout** (jam plus a stacking
penalty) and **Misfire** (kills cut your damage). Those tint the hull too.

## The gamble

A slot machine. $25,000 + $2,000/zone a pull, priced so you can't spam it fishing
for jackpots. There are around 40 distinct outcomes; the result is shown on the
E-Shop help line.

Good pulls include cash multiples of the fee, a Mega Jackpot of a flat $1,000,000
(about 1 in 5,000), a revive token, a free perk pick, a hull tier, +1 gun power,
a special weapon, max bombs, a full heal, or one of the good next-sector buffs
(Overblast, Overcharge, Merchant Favor, Golden Touch).

Bad pulls include losing the fee outright, a −20% or −50% cash haircut, losing a
bomb or your held revive, −1 gun power, a stolen perk, a permanent shop tax, or a
curse laid on the next sector (gun jam, misfire, frenzy, a beefed-up boss, Nitro,
Overheat). **Double or Nothing** does what it says to your entire pile. **Rigged**
makes your next pull roll twice and keep the worse result.

It's a slot machine. It is not a strategy.

## Perks

Perks are permanent for the run and stack. You're offered up to 3 at a time, drawn
from the perks you haven't maxed out — **up to 5 after a milestone zone**.

**When you get one:**

- after clearing zone 1, then **every 4th zone after that** (1, 5, 9, 13, …),
- at every **milestone zone** (25, 50, 75, 100, …), where the pick is 5 wide,
- from a cleared **Breakthrough** sector,
- from the E-Shop's **Buy Extra Perk**, or a lucky gamble.

| Perk | Effect | Per stack | Max |
| --- | --- | --- | --- |
| Heavy Rounds | Your shots deal more damage | +12% | 5 |
| Rapid Cyclers | Your guns fire faster | +20% cycle | 4 |
| Ablative Plating | Raises maximum armour | +8 | 6 |
| Scavenger | More cash from clears, bounties and perk buyouts | +15% | 4 |
| Nanorepair | Regenerate armour in flight | +1 armour / 4s (faster with more stacks) | 3 |
| Siphon | Chance to restore armour on a kill | +12% chance | 3 |
| Bounty Hunter | Elite and champion bounties doubled | ×2 | 1 |
| Bulwark | Take less damage from every hit | −1 (never below 1 damage) | 5 |
| Adrenaline | Fire much faster when badly hurt (below ⅓ armour) | +45% | 3 |
| Glass Cannon | Big damage, weaker hull | +40% damage, −8 max armour | 1 |
| Rapid Recharge | **Specials, sidekick ammo and sidekick charge** all recharge faster | +25% refill; charge-type sidekicks −4 ticks off the 20-tick interval (floor 4) | 4 |
| Autofire Special | Your special fires on its own while you hold fire | — | 1 |
| Efficient Coils | Weapons draw less generator power | −15% (floor 20% of stock cost) | 5 |
| Shield Matrix | Shield recharges faster | −3 ticks off the 15-tick interval (floor 3) | 4 |
| High-Velocity Shots | Your shots travel faster | +25% | 3 |
| Radar | Chart-a-Course shows each sector's real level | — | 1 |
| Surveyor | Chart-a-Course offers an extra route | +1 | 2 |
| Executioner | More damage to badly wounded enemies (below 25% HP; 15% for bosses) | +15% | 3 |
| Opening Salvo | 2s without firing supercharges the next **second** of fire | +150% damage on everything, costs no power; **generator gauge turns green when charged** | 1 |
| Kinetic Converter | Absorbed shield hits refund generator power | 20% of the hit's cost | 3 |
| Countermeasures | Taking hull damage clears nearby enemy shots | 26px radius, 40px at 2 stacks; ~2s cooldown | 2 |
| Chain Reaction | Kills blast nearby enemies (44px) | +8 damage, scaled with depth | 3 |
| Financier | **More bank interest, and the outpost charges you less** | +5 points of interest; −8.25% on buy/sell prices (−33% at 4 stacks) | 4 |
| Ordnance Reserves | **More sidekick ammo; specials last longer** | +30% magazine (min +1), +30% special duration | 4 |
| Failsafe | A hit that reaches the **hull** leaves you briefly untouchable | ~0.25s of invulnerability per stack, so ~0.5s at 2 (the ship flashes transparent, as after a respawn) | 2 |

**Failsafe fires on hull hits only.** A shot your shield soaks does nothing — the
window opens when damage punches through to armour, the same trigger
**Countermeasures** uses, so the two fire together: the burst clears the shots
around you and the i-frames cover you while you leave. It can't chain, because you
can't take hull damage while it's running.

### Take the Cash

Every perk screen ends with a **Take the Cash** row. It isn't a consolation prize
— it's the outpost offering to buy the pick back, and the price on it moves. Three
things push it up:

- **Depth.** The buyout is priced off the same clear-payout base every other
  endless reward is built from, so it keeps pace with the shop instead of falling
  behind it.
- **How wide the slate is.** A **milestone** pick deals five perks instead of
  three, and passing on five pays about **⅔ more** than passing on three. A pool
  thinned out by a long run never pays *less* than a standard slate.
- **How many perks you're already carrying.** Every stack you own adds **6%**, up
  to **+150%** at 25 stacks — the mirror image of what Buy Extra Perk charges you
  for the same collection.

**Scavenger applies to it**, so a cash build gets up to 60% more again.

The upshot is that the answer changes over a run. Early on the perk is almost
always right — the buyout is worth about **half a zone's income** at depth 1, and
the perks you take then compound for the rest of the run. Deep in, with a broad
collection and a slate full of things you've deliberately skipped, a milestone
buyout runs to **three zones' income or more** — enough to take a hull tier off
the E-Shop on the spot. That's a real fork, not a default.

Two things worth knowing before you lean on it:

- **Declining never raises your own stack count**, so the rate doesn't climb just
  because you keep refusing. Building perks first and cashing out later pays;
  refusing every pick from zone 1 does not.
- **The perk screen opens before the shop, and the E-Shop's cash-fraction buys
  price off the cash you walked in with** — which was counted before the buyout
  landed. So Turbodrive, Overblast, Overdrive and Buy Special all keep the price
  they'd have had if you'd taken the perk, and the buyout is pure profit against
  them that visit.

### Opening Salvo in detail

**How it works.** Go **2 seconds** without firing your main gun and the salvo
**charges** — the generator gauge fills green. Pull the trigger and you *spend*
it: for the next **one second**, everything your ship puts out hits at ×2.5 and
costs no generator power. That second runs on the clock, so it is a burst to use,
not a reserve to hold — the green drains down the gauge while it lasts, and the
bar is back to its normal fire colour the moment the boost ends. Then the 2s
charge starts over.

**Every sector opens charged.** You don't have to idle on the way in — the gauge
is already full green when the zone starts, so your first trigger pull of the
level opens a salvo.

**What the salvo covers — everything:**

| | While a salvo is burning |
|---|---|
| Front gun, rear gun, both sidekicks | ×2.5 damage, no generator drain |
| Weapon specials (Pearl Wind, Banana Bomb, Atom Bomb, the Lightnings…) | every shot they spawn hits at ×2.5 |
| Flare specials (Flare, SandStorm, MineField, Astral Zone, MegaLaser…) | likewise, for everything they keep spawning |
| Soul of Zinglon | the pillar burns ×2.5 as hot |
| Repulsor | shoves enemy fire away ×2.5 as hard |
| Attractor | hauls in pickups ×2.5 as hard |
| Invulnerability / Drone | ×2.5 the cover |
| Repair Player 1 / 2 | ×2.5 the patch (still capped at your hull max) |

That is genuinely every special in the game bar the six MicroSol Options, which
only spawn a sidekick — and that sidekick then fires boosted shots anyway.

**Reading it.** Two tells, so you always know which state you're in:

- **The green on the gauge is the salvo itself.** A full green bar means one is
  banked and ready. Once you spend it the green sinks steadily down the gauge —
  that is your second running out, so you can see exactly how much boost is left
  and time the rest of the burst around it. No green means it's spent.
- **The shots spark in their own colour**, taken from each weapon's palette — a
  green Protron volley trails green, a red Vulcan burst trails red — with a
  fatter flash at the muzzle. Sparks flying means the window is live right now.
  Special-weapon and flare shots spark too, in their own colours.
- **Weapons that already spark** (Mega Pulse, Wallop Beam, Protron B, Ice) don't
  get a second trail bolted on — their own plume swells instead, roughly three
  times the sparks over twice the spread.
- **Specials that fire no shot** — Repulsor, Attractor, Invulnerability, Repair —
  have no bullet to trail, so they flash a green burst off the ship instead, and
  a boosted Soul of Zinglon showers green sparks up the beam. Green is the salvo's
  colour throughout: same green as the gauge.

### On magazines and refill speed

Two perks touch sidekick ammunition, and they do different things:

- **Ordnance Reserves** makes the magazine **bigger** (+30% per stack, minimum
  +1 round, hard cap 250). Crucially, the refill cadence is rescaled to match, so
  a bigger magazine still fills in the same total time — a stack hands you a
  deeper reserve, not a longer wait. The sidekick's shop name shows the boosted
  number ("Bubble Gum-Gun   Ammo 104"), so the number you buy is the number you fly.
- **Rapid Recharge** makes the refill itself **faster** (+25% per stack, up to
  double at 4 stacks), and speeds up special-weapon cooldown by the same amount.
  It does not touch your main guns.

Charge-type sidekicks have no magazine, so Ordnance Reserves does nothing for
them — Rapid Recharge covers those instead, shortening the charge interval by 4
ticks per stack (20 down to a floor of 4). So it is the one perk that speeds up
every sidekick, whichever kind you fly.

## Money

**Clearing a zone pays:**

```
base            = $900 + $220 per zone cleared, capped at $60,000
+ modifiers     = base × (sum of the "Pays" column above)
+ level bonus   = a small per-level term, so two same-grade levels still differ
minimum         = base ÷ 4, whatever the modifiers say
```

The Chart-a-Course card shows this exact number before you commit, including
purchases and queued Sabotage charges. What's shown is what's banked.

**Interest** is paid on clear: 10% of unspent cash, capped at $3,000 + $80/zone.
Financier raises both the rate and the cap, so a higher rate genuinely pays more
rather than hitting the old ceiling a level sooner.

Financier's other half cuts what the outpost charges — 8.25% per stack, so 33%
off at 4 stacks. It applies to the **buy/sell shop** (ships, weapons, gun power,
shields, generators, sidekicks), which is the same set of prices the Loan Shark
tax and Merchant's Favor touch; the E-Shop's own buys keep their listed prices.
All three stack multiplicatively, so a Favor sector with a full Financier is the
cheapest the shop ever gets.

**Bounties** for elites and champions are paid immediately on the kill, with a
message in the text bar. They're capped per kill ($2,500 / $6,000) because the
rising tide multiplies how *many* of them there are.

**Perk buyouts** are the fourth source, and the only one you choose: see
[Take the Cash](#take-the-cash). They're built off the same clear base, so they
scale with everything else on this page.

Shop prices inflate with depth on the same curve, so a deep run is not a rich run
— it's a run with bigger numbers on both sides.

## Pickups that behave differently

Endless has no data-cube archive and no secret-level warps, so several vanilla
pickups would be dead weight. They're substituted instead:

| Vanilla pickup | In Endless |
| --- | --- |
| Collectable data cube | Grants a **random special weapon**, equipped instantly, announced in the text bar |
| Secret-level orb | Same — a random special weapon |
| A data cube buried in a regular enemy (no pickup of its own) | Drops a visible **5,000-point gem** at the wreck instead |
| The "random special weapon" dropper (from level events) | Hands back a **front or rear weapon powerup** at even odds — you're already guaranteed specials from cubes and orbs, so a third source would just re-roll what you were given |
| A weapon powerup for a port that's already maxed | Redirects to the **other** port; if both are full, drops the **5,000-point gem** rather than the small cash consolation |

The powerup redirect happens at spawn time, so a gun that fills up between the
level event firing and the enemy actually dying still gives you something useful.

## Milestone zones

| Every… | What happens |
| --- | --- |
| **25th zone** (25, 75, 125…) | The route slate is pinned to S / S+ sectors, the zone plays "Tunneling Trolls", and clearing it grants a guaranteed perk. |
| **50th zone** (50, 150, 250…) | S+ / S++ slate, plays "A Field for Mag", and grants a guaranteed perk. |
| **100th zone** (100, 200…) | S++ / S+++ slate, plays "One Mustn't Fall", grants a perk, and the slate always includes **"The End"** — the finale sector, ranked FINALITY, paying roughly 15× a normal clear on its own. Reaching zone 100 the first time also rolls the credits, once per run. |

Every milestone perk is picked from **5** offers instead of the usual 3 — the
forced slate pays out in choice as well as cash.

Music never repeats back-to-back, and each zone's track is deterministic for the
seed — retrying a zone replays the same song.

## Dying, quitting, saving

- **Death ends the run.** A held **Revive token** cheats that exactly once, with
  a full hull restore. The lethal hit also wipes every enemy bullet off the
  screen and stuns every enemy gun on the field for about **3 seconds**, so you
  get a clean board and time to fly out before the shooting starts again.
- **Outside Hardcore**, progress is saved at every outpost, into an `endless.sav`
  sidecar next to your normal save. You can quit to the title and resume.
- **In Hardcore**, nothing is saved. Dying *or* voluntarily quitting to the title
  ends the run permanently.
- **Quit Level** mid-zone drops you back at the outpost, but the outpost is locked
  to the choices you launched with — you can't re-shop your way out of a sector
  you're losing. The level relaunches with the same committed course.
- **The furthest zone you've ever reached** is kept in `opentyrian.cfg`, not in
  the save, so a Hardcore death still sets the record. The run-over screen shows
  your zone and how much you beat the old record by.

## Endless effects in a normal campaign

Endless is split into "the mode" and "the effects". The mode is the run structure
— outposts, courses, seeds, depth. The effects are the enemy scaling, the mod
bits, the perks and the elite tiers.

With **Debug Mode** on, `Endless Effects` in the debug menu layers the *effects*
onto an ordinary campaign game: scaled enemies, elites, perks and modifiers in a
normal story run, with the campaign's own levels and progression intact. Debug
screens also expose a per-lever scaling readout and let you pin individual levers,
which is the fastest way to see what any one number actually does.

---

# Display and motion settings

**Setup → Graphics.**

| Setting | What it does |
| --- | --- |
| **Display** | Windowed / fullscreen mode. |
| **Scaler** | Pixel-art scaling algorithm (nearest, Scale2x, hqx, …). |
| **Scaling Mode** | How the image fits the window. |
| **Smooth Motion** | Interpolates between the game's 35 Hz simulation ticks and presents at your display's refresh rate. This is the big one for high-refresh monitors. It also switches the player ship to a display-rate movement model, which puts control latency under one tick. Automatically off for demo recording, demo playback and network games. |
| **Sub-pixel** | Renders the playfield internally at up to 8× resolution so slow scrolling moves in fractions of a pixel instead of whole steps. "Auto" matches your scaler. Needs Smooth Motion for the full effect. |
| **Filter** | How the sub-pixel image is resampled: Sharp, Smooth, or None (raw). |
| **VSync**, **FPS Cap**, **Show FPS** | The usual. The cap only applies with VSync off; 0 is uncapped. |

The playfield itself renders at **356×200** (16:9 with square pixels) instead of
320×200. The HUD keeps its original size and stays pinned to the right edge;
menus and the shop are still drawn against the original 320-pixel layout, centred.

---

# Enhancements

**Setup → Enhancements.**

| Setting | What it does |
| --- | --- |
| **Debug Mode** | Adds the in-game debug menu, level select, and the endless debug screens. |
| **Extra Parallax** | Widens the parallax on all three background layers, so strafing sweeps the whole map instead of a narrow band. |
| **Mirrored Layers** | When a layer over-pans past its edge, it continues as a flipped mirror rather than clamping. |
| **Extra Sparks** | Denser, longer-lasting explosion spark showers. |
| **Enemy Bars…** | Small health bars over enemies you've damaged. They latch on the first hit and follow linked enemies as one. In Endless they're drawn in the enemy's elite/champion tint. |
| **Boss Health Bars…** | Style and layout of the reworked boss bars, including a two-bar mode. |
| **Gauge Gradients…** | Gradient direction of each HUD gauge. The shield and armour gauges also flash when damaged. |
| **Weapon Tweaks…** | See below. |
| **Custom Weapons** | Enables the custom weapon and its buy/sell shop slot. |
| **Custom Weapon Creator…** | See below. |

---

# Weapons

## Custom Weapon Creator

**Setup → Enhancements → Custom Weapon Creator…**

A designer for one fully custom weapon with 11 power levels, usable as a front
gun, rear gun, or sidekick, with a live firing preview as you edit. Once created
and enabled, it appears in the shop like any other weapon. (Inspired by TYRHACK.)

## Weapon Tweaks

**Setup → Enhancements → Weapon Tweaks…**

Tyrian 2000's item data merged the weapon behaviour from Episodes 1–3 and 4–5
into one set. These menus let you choose per weapon which episode's behaviour you
want, rather than living with the merge.

- **Superspark Weapons…** — Mega Pulse, Beno Wallop Beam, Beno Protron -B-, and
  Ice Beam/Blast. Only the Tyrian 2000 data gives these a superspark projectile
  trail; each submenu forces the trail Auto (per-episode) / On / Off, plus a
  toggle to keep the classic spark limit despite Extra Sparks. The Wallop Beam
  additionally has a 2nd-bolt toggle (Ep4/5 fires two bolts per volley, Ep1–3 one).
- **Episode Differences…** — the weapons that differ beyond the spark trail:
  - **Zica Laser** — level-11 shot pattern (Ep1–3 columns vs Ep4 spread), beam
    length, whether long beams follow the ship, and a buff that fires the level-10
    beam alongside the level-11 shots.
  - **Xega Ball** — Ep1–3's two weak balls vs Ep4–5's one strong ball.
  - **MicroSol Option 5** — Ep1–3's 8-way fan vs Ep4–5's twin shot.
  - **Flare / Super Bomb** — which episode's blast sprite.
  - **Needle Laser**, **Bubble Gum-Gun** — which episode's firing sound.
- **Charge-Laser** — re-adds the charge sidekick that was cut from the DOS release
  back into its shops.
- **Sidekick Autofire** — charge sidekicks fire on the held fire button.

---

# Music

**Setup → Sound → Music Synth:**

The original songs can be played as MIDI instead of OPL emulation:

- **OPL** — the default. Emulated FM synthesis, exactly as before. Needs no files.
- **FluidSynth** — needs a SoundFont (`.sf2`) next to the game. Without one it
  stays silent.
- **System synth** — the operating system's own MIDI synthesizer. Needs no extra
  files. Windows 64-bit only; not built for the console ports.

Songs loop at their internal loop point rather than restarting the file, and the
end-of-level jingle no longer repeats.

There's also a **Jukebox** in the title screen's Extra menu.

---

# Consoles

Unofficial homebrew builds for **Nintendo Switch** (`.nro`) and **PlayStation
Vita** (`.vpk`), both requiring a console that can run homebrew. Each adds a touch
sensitivity setting to the Setup menu. MIDI music is Windows-64-bit only and is
not built for either.

See [switch/README.md](switch/README.md) and [vita/README.md](vita/README.md).

---

*Developer-facing design notes, tuning-knob locations and known pitfalls live in
[notes.md](notes.md).*
