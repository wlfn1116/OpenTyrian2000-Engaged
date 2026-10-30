# OpenTyrian2000 Engaged player guide

Engaged keeps the original Tyrian campaigns and adds Endless mode,
online play, editors, restored content, and smoother rendering.

## Find a feature

| Task | Menu |
| --- | --- |
| Start Endless | Main menu > 1 Player Endless |
| Play online | Main menu > Online Multiplayer |
| Transfer saves or custom data | Title screen > Extra > Transfer |
| Edit ships | Title screen > Extra > Ship Editor, or Customize at an outpost |
| Fly a custom ship | Customize > Custom Ship at an outpost |
| Change your ship's look | Customize at an outpost, or Esc > Extra > Ship Skin |
| Design a weapon | Title screen > Extra > Weapon Editor, or Customize at an outpost |
| Change graphics | Setup > Graphics |
| Soften sound effects | Setup > Sound |
| Play your own music or sound effects | Setup > Sound > Custom Sounds |
| Turn on gamepad rumble | Setup > Rumble, with a rumble controller connected |
| Choose restored content | Setup > Enhancements |
| Use Endless item reworks elsewhere | Setup > Enhancements > Reworks |
| Configure network logs | Setup > Diagnostics |
| Open Destruct or SuperTyrian | Title screen > Extra |
| Use solo cheats | Esc > Extra |

## Graphics

Graphics settings are under **Setup > Graphics**.

- **Smooth Motion** presents interpolated frames at the display rate.
  Supported modes also move your ship at that rate.
- **Sub-pixel** renders the playfield at Auto, Off, 2x through 5x, or Native.
  The mouse cursor also moves at this finer precision on every screen.
- **Native** follows the fitted output size and costs the most GPU time.
- **Sub-pixel FX** applies the same treatment to ice, water, and lava. Console
  builds default it off.
- **Render Threads** sets how many CPU threads draw each frame, including the
  ice, water and lava effects, with or without Sub-pixel. **Auto** times each
  choice and keeps the fastest, preferring fewer threads when the gain is small,
  and times them again when a scene gets much cheaper or costlier to draw.
  A number fixes the count, and 1 keeps drawing on one thread. The choices stop
  at your CPU's thread count, 16 at most; consoles offer up to three.
- **FPS Cap** accepts a preset or a typed number. Zero means Uncapped. Online
  play requires at least 35.

The simulation remains fixed at 35 Hz. Demos always use fixed-step movement.

On Vita, Sub-pixel Auto follows the Scaler, so the default None scaler renders
at 1x. A larger Scaler or a fixed Sub-pixel value raises the rendering
resolution; it costs more CPU and GPU time and may not hold 60 fps.
**Smooth FX** draws ice, water and lava at the display rate and at the
Sub-pixel resolution; turning it off is cheaper.

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
| Varied | Each route is a different random level |
| Varied Deck | Like Varied, but routes are dealt from a deck of every level |
| Same | Every route uses one random level; only the modifiers differ |
| Same Deck | Like Same, but each chart's level is dealt from that deck |

Deck rules deal every level once before any level repeats. A level offered as a
route is used up whether or not you fly it. A Radar reroll spends the discarded
hand and deals the next one.

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

### Specials

Endless always plays the item [reworks](#reworks). For specials:

- **Attractor** pulls pickups onto your ship and follows you as you move, so
  each one is collected. Specials, weapon orbs, and the data cubes and secret
  orbs that grant specials here stay put, and a pickup is let go if your ship
  goes down. **Opening Salvo** makes the pull start faster.
- The **Orange Shield** ring circles your ship.
- **8-Way Microbomb** is named **MicroBomb** and **Pearl Wind** is named
  **Pearl Shot**.
- The **Zica Laser** Lv11 also fires the Lv10 beam, and its Auto pattern is the
  Ep 4-5 spread.

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

The normal shop restocks each visit. Its prices, weapon power levels included,
rise with depth. **Data Cubes** opens the E-Shop and **Ship Specs** opens the
perk list.

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
| Afterburners | 10% faster movement per stack; each resists 16% of slowdown and gravity, up to 80% |

Every perk screen also offers **Take the Cash**.

### Saving and records

Relaxed and Standard checkpoint at the outpost. Hardcore never writes a run
save. **Quit Level** restores the launch snapshot.

An Endless slot appears as **Endless** on Saved Games and `End` in the outpost
load and save lists.

Records are split by run mode, difficulty, Base Level rule, and crew size. A
trailing `C` marks a run that used a custom weapon or edited ship.

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

On Switch, **Local Wireless** connects two consoles without a router. **Host a
Local Game** opens the usual host settings and then creates the network; **Find
Local Games** lists nearby hosts by nickname. Both consoles must start the game
the same way, through the same game or both from the Album, or they cannot see
each other.

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
Extra Parallax and Christmas Mode also follow the host because both can affect
the game.

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
run mode decides the result; the host chooses in Relaxed. The wreck animation
cannot be skipped online.

### Appearance

Online, [Customize](#customize) adds two rows for how the other player looks on
your screen:

- **Partner Opacity** fades the other player's ship or shots.
- **Partner HP Bars** shows the other player's shield and armour.

Ship Color shows the other player's dye beside **Partner**, so you can pick a
different one. Your ship color, Color Sidekicks choice, and skin show on the
other player's screen. Linked Arcade omits partner bars because its shared HUD
already shows both players. Online saves keep both players' appearance choices.

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

## Customize

**Customize** is on the outpost menu of every game except a local two-player
one.

| Row | Use |
| --- | --- |
| Custom Ship | Fly one of your Ship Editor ships, or return to your own ship |
| Ship Skin | Wear another hull's look; stats stay the same |
| Ship Color | Dye your ship; **Color Sidekicks** decides whether the sidekicks take the dye |
| Partner Opacity, Partner HP Bars | Online only; see [Appearance](#appearance) |
| Ship Editor | Edit your custom ships |
| Weapon Editor | Design and equip a custom weapon in Campaign and Endless |

Custom Ship and Ship Editor appear where custom ships are allowed (see
[Extra menu and Ship Editor](#extra-menu-and-ship-editor)). A locked Endless
sortie hides Custom Ship and Weapon Editor. Both editors keep the outpost music.

### Custom ships

**Custom Ship** opens a grid with your own ship, labelled **Stock**, and the ten
Ship Editor slots. Choosing a slot equips that ship with its whole loadout. Your
own ship waits with everything bought for it; choose **Stock** to go back
exactly as you left it, weapon power included. A hull bought while flying a
custom ship is listed as **Current**.

While you fly a custom ship, **Upgrade Ship** trades as usual with these rules:

- The parts the custom ship came with cost nothing to keep and sell for
  nothing.
- A gun the custom ship brings starts at your own ship's power. Those levels
  are borrowed: free to keep and worth nothing when sold. Higher levels cost
  the gun's normal price.
- Buying a hull swaps only the hull. The other parts stay, and your own ship
  still waits in Custom Ship.
- Going back to your own ship drops everything bought for the custom ship.
  Switching to another custom ship replaces the parts that ship names.

Online, your ships reach the other player when the session starts, when you
open Custom Ship after editing them, and before each level. A resumed save
keeps everyone's custom ship.

## Weapon Editor

Open **Title screen > Extra > Weapon Editor**, or **Customize > Weapon Editor**
at a Campaign or Endless outpost, where **Equip** mounts the design and returns
to the shop. The library holds 32
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

Enter or a click on **Shop Icon**, **Sprite**, **Body Sprite**, or the Range
page's **Ship** opens a picker grid. Tap or press Enter to choose, drag or use
the mouse wheel to scroll, and type a number to jump to it. Tab or a controller's
sidekick buttons switch between the tabs along the top, as do L and R on Switch
and Vita; Esc or right-click cancels. The bullet sprite grid also
holds the miscellaneous interface sprites. The ship grid shows each distinct
hull once and lists Ship Editor ships under **Custom**.

The Range page's targets, stats, layout, and ship are saved in
`opentyrian.cfg` and return on the next visit.

Levels can be copied, pasted, spread across the power curve, or generated as a
curve. The preview uses the real shot simulation.

Online Campaign and Endless exchange one design per player. Separate Arcade
also sends designs referenced by custom ships. If a design fails to reach the
other player, both machines use a placeholder weapon in its place. Campaign and
Endless send it again at the next outpost.

## Enhancements

**Setup > Enhancements** groups settings by purpose:

| Menu | Contents |
| --- | --- |
| Visuals | Parallax, mirrored layers, sparks, tint, shop sprites, CRT filter |
| Heads-Up Display | Enemy bars, boss bars, gauges, touch buttons |
| Weapons | Charge-Laser, autofire, spark trails |
| Gameplay | Hitboxes, Guided Aim, Ice Base Shots, Arcade options |
| Episode Versions | Data choices that differ between episode sets, and the Zica Laser Lv11 pattern |
| Reworks | Endless versions of stock items, for the other modes |

### Presets

| Preset | Effect |
| --- | --- |
| Vanilla | Closest available match to Tyrian 2000 |
| Engaged | Default set for this fork |
| Custom | Most recent hand-edited set |

Changing an enhancement stores the result as Custom. Sidekick Autofire, touch
button settings, and the CRT Filter settings stay outside presets.

Episode Versions rows accept Auto, Ep 1-3, or Ep 4-5. Auto follows the current
episode, except that Endless plays the Zica Laser Lv11 on Auto as Ep 4-5.
Online sessions use the host's choices.

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

### Reworks

Endless reworks some stock items. **Enhancements > Reworks** brings each one to
the other modes:

| Rework | Effect |
| --- | --- |
| Zica Laser Buff | Zica Laser Lv11 also fires the Lv10 beam, at no extra power |
| Attractor | Hauls pickups onto your ship until it collects them |
| Orange Shield | The shield ring circles your ship instead of the air above it |
| Ramming | Ram kills pay and drop like kills; invulnerable ships ram every tenth tick |
| Item Names | Endless names, such as MicroBomb, Pearl Shot, and Front Multi-Cannon |

Endless always plays every rework. Vanilla turns them all off, and Engaged turns
them all on. Online sessions use the host's choices.

The reworked Attractor leaves specials, secret-level orbs, and weapon orbs where
they are. It hauls purple orbs, except in Super Arcade, where every orb stays
put.

### CRT Filter

**Setup > Enhancements > Visuals > CRT Filter** holds six settings:

| Setting | Choices |
| --- | --- |
| Filter | Off (default), Scanlines, Full |
| Strength | Light, Medium (default), Strong |
| Mask | Off, Grille (default), Slot |
| Curvature | Off (default), Subtle, Strong |
| Corners | Square (default), Rounded |
| Rolling Bar | Off (default), Subtle, Strong |

- **Filter**: Scanlines draws a dark line under every game row. Full adds a
  phosphor mask, a glow around bright objects, and darker edges. Bright colors
  fill in their own lines, so shots, explosions and white text keep their
  brightness while dark areas show deep lines.
- **Strength** sets the depth of the lines, glow and mask. It is dimmed while
  Filter is Off.
- **Mask** picks the phosphor pattern for Full. Grille draws unbroken red,
  green and blue stripes like an aperture-grille tube. Slot breaks them into
  staggered slots like a slot-mask TV. Off keeps the glow and darker edges
  without a mask. The row is dimmed unless Filter is Full.
- **Curvature** bends the picture like a curved tube, with Filter on or off.
  The whole picture stays on screen: the edges bow and the corners pull in,
  with black behind them. Clicks and touches follow the bend; touch buttons
  stay flat.
- **Corners**: Rounded gives the picture round corners like a tube screen,
  with Filter on or off. With Curvature they bend along with the picture.
- **Rolling Bar** sends a soft dark band down the screen every 9 seconds, like
  the hum bar of an old TV, with Filter on or off. Strong is twice as dark as
  Subtle.
- Lines appear once the picture is at least 2x the game's size. The other
  layers work at every size, on every platform.
- The submenu is missing when the graphics driver cannot draw it. Curvature
  also needs SDL 2.0.18 or newer.

## Diagnostics

**Setup > Diagnostics** holds Debug Mode and network logging. Console builds
also provide **Clear Logs**.

Debug Mode adds a menu to shops and the in-game menu, plus a level browser. It
can change equipment, cash, cheats, difficulty, and Endless effects. Rollback
Self-Test writes failures to `rollback_selftest.log`.

**Perf Overlay** in the in-game debug menu shows the frame rate, enemy and shot
counts, how a frame's time splits between logic, drawing, and presenting, and
the thread count each drawing stage uses.

## Extra menu and Ship Editor

**Title screen > Extra** contains Instructions, Transfer, Ship Editor, Jukebox,
Destruct, SuperTyrian, Super Arcade ships, command-line cheats, and Christmas
Mode.

**Esc > Extra** exposes solo cheats, debug codes, the custom-ship picker, and
ship skins. Online, **Debug Codes** disables Hyper-Speed and Level Filter.

Custom ships and skins are available in solo Campaign, Endless, and Arcade, plus
online Campaign, Endless, and Separate Arcade. If a custom ship fails to reach
the other player, both machines put that player back in their own ship, or the
USP Talon when there is none. The ships are sent again before the next level.

### Edit ships

The Ship Editor edits ten slots saved in `custom_ships.shp`; slot 10 uses the 0
key. Open it from **Title screen > Extra** or from **Customize** at an outpost.
Put `User.shp` beside the game executable or in `data`, then choose **Import**.
Its filename is case-insensitive. **Done** writes `custom_ships.shp` without
changing the shipped `data/newsh$.shp`.

Enter or a click on **Graphic** opens a hull grid with the built-in hulls under
**In-Game** and the eight custom banks under **Custom**.

To change ship during play:

- hold Tab and press a number in a solo game; or
- use **Esc > Extra > Custom Ship** with keyboard, controller, or touch.

**Standard** restores the loadout held before the first switch, weapon power
included. Damage is preserved as a percentage when maximum armour or shield
changes. At an outpost, use [Customize > Custom Ship](#custom-ships).

The sprite editor stores five banking poses per custom bank. Color 0 is
transparent. Mouse controls use right-click to erase and middle-click to pick a
colour; on a controller, Tab moves focus to or from the canvas.

Online Campaign, Endless, and Separate Arcade exchange each player's custom
ships and any custom weapons they need.

### Bank images

The sprite editor's **Import Image** button loads `custom-bank-<n>.png` from the
[save folder](#files-and-logs) into the open bank. Left/Right on the button
switches to **Export Image** or **Revert**.

- **Export Image** writes the bank's five poses side by side, hard left to hard
  right, as a 120x28 PNG in the game palette. Colour 0 is saved as transparent.
- Edit the file in any image editor that keeps exact colours. The game palette
  repeats some colours, so the export moves those entries one step apart and
  every entry has a colour of its own. An unchanged pose imports back exactly,
  even after an editor rebuilds the palette or saves the image in full colour.
- Transparent pixels and pure black become colour 0, so the black background of
  an older BMP export still imports as transparent.
- Any other colour becomes the nearest game colour. Where several palette
  entries share that colour, the import keeps the bank's current pixel when it
  matches, then follows the colour ramp of the surrounding pixels.
- The image must be exactly 120x28. **Undo** takes an import back. To import an
  older `custom-bank-<n>.bmp`, open it in an image editor and save it as PNG.

### Ship skins

**Customize > Ship Skin** at an outpost and **Esc > Extra > Ship Skin** change
how your ship looks without changing its weapons, armour, or hitbox. Customize
shows the skins in a grid with **In-Game** and **Custom** pages. Esc > Extra
cycles **Default**, then the in-game hulls, then custom banks that have artwork,
with Left/Right or a click, and its preview shows the skin in motion.

- The skin is saved with the game and shows on the other player's screen online.
- In the shop's ship list, your current ship shows the skin and "(Skin)" after
  its name, and the large ship picture follows it.
- The two-piece Dragonwing and Nort Ship can neither be worn as skins nor wear
  one.

## Sound

**Setup > Sound** has two sound-effect options, both off by default. Music is
never filtered or limited.

- **Sound Filter** softens the 8-bit effects with a low-pass filter. **Light**
  rolls off above about 4 kHz and **Strong** above about 2.8 kHz.
- **Same-Sound Limit** sets how many copies of one effect may play at once, from
  1 to 3. In a busy fight the same enemy shot sound can otherwise stack several
  times over; copies past the limit are skipped.

### Custom sounds

**Setup > Sound > Custom Sounds** replaces the game's music (**OST**), its sound
effects (**SFX**), or both with your own files. It is **Off** by default.

Put songs in a `mus` folder and sound effects in an `sfx` folder, either beside
the executable or in the settings folder from [Files and logs](#files-and-logs).
Name each file after the song or sound it replaces, by its number or its name
from the tables below: `01.mp3`, `1.mp3`, `Asteroid Dance Part 2.mp3`, or
`EXPLSM.wav`. Names ignore case, spaces, and punctuation, so
`Tyrian - The Song.ogg` matches **Tyrian: The Song**. MP3, Ogg Vorbis, and WAV
files work, in mono or stereo and at any sample rate. Anything without a file
keeps its original, and music then plays through the current **Music Synth**.

- A replacement song loops back to its start where the original loops. Songs
  marked **Once** below end instead, as the originals do.
- A sound effect plays at most its first 60 seconds.
- When several files fit, a two-digit number such as `01` wins, then other
  numbers, then the name.

**Title screen > Extra > Jukebox** shows each song's number and name. The key
left of the right Shift key switches between songs and sound effects. With sound
effects shown, the arrow keys or comma and period play the previous or next one,
Enter plays it again, and M mutes the music while you listen. F or Space hides
the text.

Controllers use the same buttons by position on every system: the top face
button switches songs and sound effects whether it reads X, Y or Triangle. While
the jukebox is open those buttons do only their jukebox job.

| Action | Keyboard | Controller | Switch | Vita | Touch |
| --- | --- | --- | --- | --- | --- |
| Switch songs and sound effects | Key left of right Shift | Top face button | X | Triangle | Circular arrow |
| Hide the text | F or Space | Left face button | Y | Square | Expand arrows |
| Previous or next sound | Arrows, comma, period | D-pad | D-pad | D-pad | Arrow buttons |
| Play the sound again | Enter | A | A | Cross | Speaker |
| Mute the music | M | Left shoulder | L | L | Crossed-out speaker |
| Leave the jukebox | Esc | B, Start or Back | B, Plus or Minus | Circle, Start or Select | Close button |

#### Songs

| File | Name | Plays |
| --- | --- | --- |
| 01 | Asteroid Dance Part 2 | Loops |
| 02 | Asteroid Dance Part 1 | Loops |
| 03 | Buy/Sell Music | Loops |
| 04 | CAMANIS | Loops |
| 05 | CAMANISE | Loops |
| 06 | Deli Shop Quartet | Loops |
| 07 | Deli Shop Quartet No. 2 | Loops |
| 08 | Ending Number 1 | Loops |
| 09 | Ending Number 2 | Loops |
| 10 | End of Level | Once |
| 11 | Game Over Solo | Once |
| 12 | Gryphons of the West | Loops |
| 13 | Somebody pick up the Gryphone | Loops |
| 14 | Gyges, Will You Please Help Me? | Loops |
| 15 | I speak Gygese | Loops |
| 16 | Halloween Ramble | Loops |
| 17 | Tunneling Trolls | Loops |
| 18 | Tyrian, The Level | Loops |
| 19 | The MusicMan | Once |
| 20 | The Navigator | Loops |
| 21 | Come Back to Me, Savara | Loops |
| 22 | Come Back again to Savara | Loops |
| 23 | Space Journey 1 | Loops |
| 24 | Space Journey 2 | Loops |
| 25 | The final edge | Once |
| 26 | START5 | Loops |
| 27 | Parlance | Loops |
| 28 | Torm - The Gathering | Loops |
| 29 | TRANSON | Loops |
| 30 | Tyrian: The Song | Loops |
| 31 | ZANAC3 | Once |
| 32 | ZANACS | Loops |
| 33 | Return me to Savara | Loops |
| 34 | High Score Table | Once |
| 35 | One Mustn't Fall | Loops |
| 36 | Sarah's Song | Loops |
| 37 | A Field for Mag | Loops |
| 38 | Rock Garden | Loops |
| 39 | Quest for Peace | Loops |
| 40 | Composition in Q | Loops |
| 41 | BEER | Loops |

#### Sound effects

| File | Name |
| --- | --- |
| 01 | SCALEDN2 |
| 02 | F2 |
| 03 | TEMP10 |
| 04 | EXPLSM |
| 05 | PASS3 |
| 06 | TEMP2 |
| 07 | BYPASS1 |
| 08 | EXP1RT |
| 09 | EXPLLOW |
| 10 | TEMP13 |
| 11 | EXPRETAP |
| 12 | MT2BOOM |
| 13 | TEMP3 |
| 14 | LAZB |
| 15 | LAZGUN2 |
| 16 | SPRING |
| 17 | WARNING |
| 18 | ITEM |
| 19 | HIT2 |
| 20 | MACHNGUN |
| 21 | HYPERD2 |
| 22 | EXPLHUG |
| 23 | CLINK1 |
| 24 | CLICK |
| 25 | SCALEDN1 |
| 26 | TEMP11 |
| 27 | TEMP16 |
| 28 | SMALL1 |
| 29 | POWERUP |
| 30 | MARS3 |
| 31 | NEEDLE2 |
| 32 | VOICE1 |
| 33 | VOICE2 |
| 34 | VOICE3 |
| 35 | VOICE4 |
| 36 | VOICE5 |
| 37 | VOICE6 |
| 38 | VOICE7 |
| 39 | VOICE8 |
| 40 | VOICE9 |

## Controllers

On PC, Mac, Linux, Android, and iOS a controller can connect at any time, over
USB or Bluetooth, and is ready within a second. One that drops out and comes
back takes its old bindings and player back.

A controller the game has not seen before gets these bindings when SDL
recognises it, which covers Xbox, PlayStation, Switch Pro, and most other
gamepads. Others start on their first buttons. Change either under **Options >
Joystick Setup**.

| Control | Action |
| --- | --- |
| Left stick, d-pad | Steer |
| A | Fire |
| B | Change rear weapon mode |
| X, left shoulder | Left sidekick |
| Y, right shoulder | Right sidekick |
| Start | Menu |
| Back | Pause |

In Destruct, two human players with two controllers connected get one each.
Otherwise the first controller drives every human player.

### Switch

The Switch build reads two controllers. Player 1 is the attached Joy-Cons or
the first controller, player 2 the second. A Joy-Con pair, a Pro Controller, and
a single Joy-Con held sideways all work, and all of them rumble. Detaching the
Joy-Cons during play opens the system controller screen.

| Sideways Joy-Con | Action |
| --- | --- |
| Stick | Steer |
| Right face button | Fire |
| Bottom face button | Change rear weapon mode |
| ZL on the left Joy-Con, ZR on the right | Change rear weapon mode |
| SL, SR | Left and right sidekick |
| Stick press | Menu |
| Minus on the left Joy-Con, Plus on the right | Menu |

L on the left Joy-Con and R on the right are free to bind under **Options >
Joystick Setup**.

Starting a 2-player game opens the controller screen when a second controller
is missing: hold a Joy-Con sideways and press SL and SR to register it. Each
player then plays on their own controller. A loaded 2-player game seats them
the same way. In a 2-player game Plus and Minus open the menu instead of
pausing, on every controller. In a local 2-player game a Pro Controller or
Joy-Con pair uses the sideways Joy-Con layout: L and R fire the left and right
sidekicks, and ZL and ZR change the rear weapon mode. X and Y fire the
sidekicks too.

### Rumble

**Setup > Rumble** appears while a connected controller has a vibration motor.
It vibrates the controller when your ship is hit: a short pulse on the shield, a
stronger one when damage reaches the hull, harder the more armor the hit takes,
and a long one when the ship is destroyed. Choose **Light** or **Strong**; it is
**Off** by default, and changing it plays a sample pulse.

Explosions rumble too. A large blast thumps and fades away within half a
second, a small one gives a short buzz, and several at once rumble harder. A
blast near your ship hits harder than one across the screen.

A hit rumbles the pad steering the ship that was hit, which online is only ever
yours. In a local 2-player game each pad feels the blasts by their distance from
its own ship. In Destruct, blasts rumble every pad, and a hit or a lost unit
rumbles the pad of the player whose unit it was. The Vita handheld has no
vibration motor, so on a Vita the setting appears only while a paired
controller is connected.

Rumble stops when you pause, open the in-game menu, reach the shop or outpost
between levels, or switch to another window.

A Bluetooth DualShock 4 or DualSense only rumbles in an extended mode, which
the game turns on. Until you switch the pad off, some older PC games that read
it through DirectInput may not see it.

## Music

| Backend | Requirement |
| --- | --- |
| OPL3 | None; default |
| FluidSynth | Windows x86-64 and a SoundFont |
| Native MIDI | Windows x86-64 |

Choose under **Setup > Sound > Music Synth**. Put a `.sf2`, `.sf3`, or `.sf`
SoundFont beside the executable, in `data`, or in the `mus` or `sfx` folder. To
play your own recordings in place of the songs, see [Custom sounds](#custom-sounds).

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
| Custom Weapons | Weapon library |
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
| `custom_weapons.cfg` | Weapon Editor library |
| `custom_ships.shp` | Compiled custom ships |
| `custom-bank-<n>.png` | Sprite editor bank images |
| `User.shp` | Optional DOS ShipEdit source |
| `mus/<n>.mp3`, `.ogg`, or `.wav` | Custom Sounds songs |
| `sfx/<n>.mp3`, `.ogg`, or `.wav` | Custom Sounds effects |
| `log/opentyrian_log_<time>.log` | Windows crash report |
| `log/opentyrian_net_<time>.log` | Online session log |

`opentyrian.cfg` and `opentyrian.sav` are plain text. Edit them only while the
game is closed.

The first launch without `opentyrian.sav` imports the save slots from a
`tyrian.sav` written by OpenTyrian, OpenTyrian2000, or DOS Tyrian, along with its
Tyrian 2000 high scores when it has them, and leaves that file in place. Earlier
builds of this port wrote their own `tyrian.sav`, `endless.sav`, and
`tyrian.cfg`; those are not imported.
