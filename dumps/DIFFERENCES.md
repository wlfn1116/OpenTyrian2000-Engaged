# Tyrian version differences

This compares decoded data from Tyrian 1.1, Tyrian 2.1, and Tyrian 2000.
The matching `dump_11/`, `dump_21/`, and `dump_2000/` trees use one layout and
identify the source file behind every decoded record.

Tyrian 2.1 changed almost every asset. Tyrian 2000 added episode 5 and changed
publisher, while more than half of the 2.1 files remained untouched. Most of its
data changes are appended records and new banks, with a few reused or inserted
slots described below.

No difference is discarded for being small. When a table is too large to print
record by record, this page gives exact changed-record and field counts; the
JSON and CSV files in the three dump trees hold the individual values.

## Contents

- [Release totals](#the-three-releases)
- [Episodes and levels](#episodes-and-levels)
- [Ships](#ships), [guns](#guns), [sidekicks](#sidekicks),
  [specials](#specials), [shields, and generators](#shields-and-generators)
- [Weapon and enemy records](#weapon-records)
- [Differences inside one release](#differences-within-each-release)
- [Interface text and datacubes](#the-interface)
- [Graphics](#graphics)
- [Sound, music, and demos](#sound-and-music)
- [Added, removed, and renamed files](#the-files-themselves)
- [Container formats](#container-format-changes)
- [Curiosities](#curiosities)

## Comparison rules

- Item and record counts in **At a glance** include slot 0.
- “Byte for byte” means the original source files or decoded record bytes were
  compared directly.
- A reachable weapon field is one the matching engine firing path can read.
  Padding and unused pattern positions are still counted in raw comparisons.
- Blank cells in an episode table mean that release has no such episode.
- Filename comparisons ignore case because Tyrian 1.1 uses upper-case data
  names and later releases use lower case.

## The three releases

| | Tyrian 1.1 | Tyrian 2.1 | Tyrian 2000 |
| --- | --- | --- | --- |
| Year | 1995 | 1996 | 1999 |
| Publisher on screen | Epic MegaGames | Epic MegaGames | XSIV Games |
| Episodes | 3 | 4 | 5 |
| Level records | 37 | 62 | 70 |
| Files in the compared directory | 94 | 106 | 114 |
| Dump | `dump_11/` | `dump_21/` | `dump_2000/` |

## At a glance

For 2.1 and 2000, the counts come from the shared episodes 1-3 table. The 1.1
counts come from episode 1; its other two sets have the same sizes.

| | 1.1 | 2.1 | 2000 |
| --- | --- | --- | --- |
| Ships | 13 | 14 | 19 |
| Weapon ports | 40 | 43 | 61 |
| Specials | 26 | 47 | 55 |
| Sidekicks | 19 | 31 | 38 |
| Shields | 11 | 11 | 12 |
| Generators | 7 | 7 | 7 |
| Weapon records | 721 | 781 | 1638 |
| Enemy records | 851 | 851 | 1701 |
| Songs | 34 | 41 | 41 |
| Sound effects | 29 | 29 | 31 |
| Palettes | 23 | 23 | 24 |
| Full-screen backdrops | 12 | 13 | 14 |
| Tilesets | 4 | 5 | 5 |
| Tiles drawn | 1148 | 1339 | 1531 |
| Sprite banks in `tyrian.shp` | 11 | 12 | 13 |
| Enemy sprite files | 32 | 34 | 39 |
| Interface text groups | 31 | 33 | 50 |
| Datacubes | 54 | 141 | 154 |

## Episodes and levels

Episode 3 is the stable one. Its twelve level names never changed across any
release, and its script file `levels3.dat` is byte for byte identical in 2.1 and
2000. The level data itself in `tyrian3.lvl` was still edited at both steps.

**Episode 1** grew from 14 level records to 18 in 2.1. The four newcomers are a
second TYRIAN (Hard+ difficulty exclusive), ASSASSIN, SAVARA V and `** ALE **`,
and they are still there in 2000.

**Episode 2** gained one record in 2.1, a second BONUS level, and stayed at 12.

**Episode 4, An End to Fate** arrived in 2.1 with 20 levels: HARVEST, WINDY,
SAVARA IV, SURFACE, CORE, SIDE EXIT, UNDERDELI, ICE EXIT, LAVA EXIT, DESERTRUN,
?TUNNEL?, LAVA RUN, EYESPY, DREAD-NOT, BRAINIAC, NOSE DRIP, TIME WAR, SQUADRON,
APPROACH and ICESECRET. Tyrian 1.1 already had a slot for a fourth episode, and
its name in the menu was the placeholder `Episode 4: Electronic Arts`.

**Episode 5, Hazudra Fodder** arrived in 2000 with 8 levels: SAVARA, AST ROCK,
STATION, SAVARA, ASTEROIDS, MINERS, CORAL and FRUIT. The slot it took over was
labelled `Episode ?: Bonus` in both earlier releases.

The in-game text keeps its own count and does not quite agree with the files.
Tyrian 1.1 advertises `Levels per Episode 1:13 2:11 3:12`; 2.1 and 2000 both
advertise `Levels in Episode 1:17 2:12 3:12 4:20`. Episode 1 is the one that
disagrees: its level file holds one record more than the line admits to.

## Ships

The twelve ships you could buy in 1995 kept their names, their prices and their
slots in every later release. Everything new was appended.

| Slot | Ship | First in |
| --- | --- | --- |
| 1 | USP Talon Light Fighter, 6000 | 1.1 |
| 2 | SuperCarrot, 65000 | 1.1 |
| 3 | Gencore Phoenix, 12000 | 1.1 |
| 4 | Gencore Maelstrom, 15000 | 1.1 |
| 5 | MicroCorp Stalker, 20000 | 1.1 |
| 6 | MicroCorp Stalker-B, 25000 | 1.1 |
| 7 | Prototype Stalker-C, 50000 | 1.1 |
| 8 | Stalker, 10000 | 1.1 |
| 9 | USP Fang Light Fighter, 8000 | 1.1 |
| 10 | U-Ship, 8000 | 1.1 |
| 11 | Silver Ship, 8000 | 1.1 |
| 12 | Nort Ship, 65000 | 1.1 |
| 13 | The Stalker 21.126, 65535 | 2.1 |
| 14 | Storm, 5000 | 2000 |
| 15 | Red Dragon, 500 | 2000 |
| 16 | Gencore II, 17000 | 2000 |
| 17 | PeteZoomer, 500 | 2000 |
| 18 | Rum Bottle, 500 | 2000 |

Two artwork fields did change in the episodes 1-3 table in 2.1: the U-Ship's
large shop graphic went from 32 to 28 and the Nort Ship's from 32 to 33. The
episode 4 table retains graphic 32 for both, which is one of the internal table
differences described below.

The Super Tyrian ship list grew the same way. Tyrian 1.1 offers Ninja Star,
StormWind, The Experimental PQZ, Captured U-Fighter, FoodShip Nine, TX
SilverCloud and Nort-Ship Z; 2.1 adds the Stalker 21.126 to that screen, and
2000 adds Dragon and Pretzel Pete Truck.

## Guns

Weapon ports 0 to 38 are identical in name across all three releases: slot 0 is
`None`, followed by the Pulse-Cannon through the NortShip Spreader B. Slot 39 is
where they diverge: in 1.1 it is a placeholder called `SuperTyrian Ship 1`, and
2.1 replaced it with the **Atomic RailGun**, then added **Widget Beam**, **Sonic
Impulse** and **RetroBall**.

Tyrian 2000 added five more: **Needle Laser**, **Pretzel Missile**, **Dragon
Frost**, **People Pretzels** and **Dragon Flame**. Slots 48 to 60 in 2000 are
thirteen ports all named `Test`, left in the shipped data.

Names do not tell the whole story. In 2.1, one power-level entry in
Miscellaneous Option Weapons changed from weapon record 0 to record 1. Tyrian
2000 also gave the inherited Poison Bomb port shop icon 13 where 2.1 stored 0.

## Sidekicks

Three sidekick names changed in 2.1, but only the first was a pure rename:

| Slot | 1.1 | 2.1 and 2000 |
| --- | --- | --- |
| 6 | SuperMissile | MegaMissile |
| 16 | Charge-Laser Cannon | Mint-O-Ship |
| 17 | Flame Thrower | Zica Flamethrower |

Mint-O-Ship is a replacement, not a label change: its power, price, movement,
animation, sprites and weapon port all differ from Charge-Laser Cannon.
Zica Flamethrower keeps most of the old record but changes its weapon from
record 121 to 780.

Ammunition was cut twice. Atom Bombs dropped from 40 rounds to 20 in 2.1, and
Plasma Storm dropped from 10 to 6 in 2000 while its price rose from 8000 to
9500.

Twelve sidekicks arrived in 2.1: Companion Ship Warfly, MicroSol FrontBlaster,
Companion Ship Gerund, BattleShip-Class Firebomb, Protron Cannon Indigo,
Companion Ship Quicksilver, Protron Cannon Tangerine, MicroSol FrontBlaster II,
Beno Wallop Beam, Beno Protron System -B-, Tropical Cherry Companion and
Satellite Marlo.

Tyrian 2000 added Bubble Gum-Gun and Flying Punch, then five empty slots named
`None`.

## Specials

This table reused six consecutive slots instead of only appending. The names in
slots 0 to 13 and 20 to 25 survive, but five of those inherited records still
changed in 2.1: Ice Beam's power went 20 to 5, Protron Dispersal's 0 to 30 and
Astral Zone's 20 to 2, while MineField and Dual Vulcan changed their displayed
graphics from 125 to 283 and 131 respectively.

Slots 14 to 19 were taken over by new weapons, and the six specials that used to
live there were moved to the end of the table:

| Slot | 1.1 | 2.1 and 2000 |
| --- | --- | --- |
| 14 | Invulnerability | Xega Ball |
| 15 | Atom Bomb | MegaLaser Dual |
| 16 | Seeker Bombs | Orange Shield |
| 17 | Ice Blast | Pulse Blast |
| 18 | Repair Player 1 | MegaLaser |
| 19 | Protron Field | Missile Pod |

Slots 26 to 46 are all new in 2.1: five directional Lightning specials, six
MicroSol Options, then Invulnerability, Atom Bomb, Seeker Bombs, Lightning Zone,
SDF Main Gun, Ice Blast, Repair Player 1, Pearl Wind, 8-Way Microbomb and Protron
Field. Six of those are the ones evicted from slots 14 to 19. Their records are
identical at the new slots except that Invulnerability acquires graphic 129.

Tyrian 2000 added **Super Pretzel** and **Dragon Lightning**, then six slots named
`None`.

The renumbering means a slot number alone does not identify a special across
releases. A 1.1 save naming special 15 would find MegaLaser Dual in that slot
under 2.1, not the Atom Bomb it meant.

## Shields and generators

The six generators never changed, down to the misspelling of `Advanced
MircoFusion` that survives in all three releases.

Shields were stable until 2000 inserted **Gencore Solar Shield** at slot 8,
pushing MicroCorp HXS Class A, B and C down one place each.

The new shield is the only non-placeholder item in Tyrian 2000 whose shop icon
differs between table sets: icon 165 for episodes 1-3 and icon 153 for episodes 4
and 5. The placeholder `Test`/`None` weapon-port block also changes icons.

## Weapon records

The weapon table looks like it was rewritten. Much of that impression comes
from data in pattern positions that firing never reaches, but the rule is not
"the first `multi` slots." A record carries eight parallel shot-pattern slots.

For player fire, `multi` is the number of bullets emitted per trigger while the
cursor cycles through `max` pattern positions, or all eight when `max` is zero.
Enemy fire also uses `max`, and falls back to the first position when it is zero.

Tyrian 1.1 left a great deal of nonzero data outside those reachable positions.
2.1 cleared much, but not all, of it: the number of inherited records with a
nonzero unreachable position fell from 630 to 190. Comparing raw records still
exaggerates the change:

| Step | Records that differ byte for byte | Records that differ in potentially firing-readable fields or slots |
| --- | --- | --- |
| 1.1 to 2.1 | 643 of 721 | 228 |
| 2.1 to 2000 | 463 of 781 | **6** |

The real 1.1 to 2.1 changes are led by sound: 155 weapons fire with a different
sample. After that come reachable shot graphics (77 records), shot lifetime
(54), attack (33) and shot offsets (25 to 29, depending on the field).

Tyrian 2000 changed six of the inherited weapons, and only their `attack`
arrays. Record 88 changed from ordinary damage to piercing attacks; the other
five had their damage reduced. The 857 added records do **not** all sit in the
second bank: 38 extend the first bank at indices 781 to 818, and 819 occupy the
new bank at indices 1000 to 1818.

## Enemies

Enemy records stayed at 851 through 2.1, and Tyrian 2000 added a whole second
bank at indices 1001 to 1850.

| Step | Records changed | Mostly |
| --- | --- | --- |
| 1.1 to 2.1 | 47 of 851 | 26 re-pointed at a different sprite bank, 12 changed score value, 9 moved their start position, 8 changed armor |
| 2.1 to 2000 | 35 of 851 | 25 changed explosion type, 8 changed turret setup, 2 changed armor |

## Differences within each release

Each release ships more than one copy of the item tables, and the copies are not
identical. This matters if you read one set and assume it holds everywhere.

**Tyrian 1.1** keeps a set at the end of every level file. All three agree on
every table except weapons. Each pair of weapon copies differs in 630 of 721 raw
records, but the differences are outside reachable pattern positions except for
the first slot of records 0 and 39.

Record 0 is the `None` sentinel and record 39 is not selected by any item or
enemy. Neither can fire, so the three copies are gameplay-equivalent.

**Tyrian 2.1 and 2000** split their tables between `tyrian.hdt`, for episodes 1
to 3, and the level file of each later episode. The non-weapon item differences
are small but pointed:

- **The Stalker 21.126 costs 65535 in episodes 1 to 3 and 30000 in episode 4.**
  It is priced out of reach everywhere except the episode built around it.
- The U-Ship and the Nort Ship use a different large ship graphic in episode 4.
- Miscellaneous Option Weapons has one different power-level pattern entry.
- One special, MicroSol Option2, has a different type field.
- In Tyrian 2000, the thirteen `Test` weapon ports exist only in the episode 1-3
  table. In the episode 4 and 5 tables those slots are named `None` and cost
  nothing.
- The enemy tables differ in 385 of 851 records between the two sets in 2.1. In
  2000, each later-episode table differs
  from the episodes 1-3 table in 699 of 1701 records; the episode 4 and 5 enemy
  tables are identical. The later episodes reuse enemy slots for their own
  creatures.

The weapon copies diverge at least as strongly. In 2.1, the episodes 1-3 and
episode 4 copies differ in 694 of 781 raw records, including 391 records with a
scalar field or pattern position that an engine firing path can read.

In 2000, the episodes 1-3 copy differs from episode 4 in 1,504 of 1,638 raw
records and from episode 5 in 1,336. The episode 4 and 5 copies differ heavily
byte for byte, but their reachable differences are confined to six unreferenced
sentinel records; their usable weapon data is equivalent. `shipBlastFilter` is
the most common scalar disagreement: 341 records in 2.1 and 705 in 2000.

## The interface

**The main menu.** Tyrian 1.1 and 2.1 read Start New Game, Load Game, High
Scores, Instructions, Ordering Info, Demo, Quit. Tyrian 2000 removed Ordering
Info from the list; the string is still in the file, renamed to
`---Ordering Info Before Demo`.

**Play Next Level became Start Level** everywhere in 2000: the shop menu, the
two-player menu, the network menu and the Super Tyrian menu all changed the same
way, and the online help entry changed from `~Next Level~` to `~Start Level~`.

**Ship Specs** appeared in both the shop and Super Tyrian menus in 2.1, along
with the help line `Access schematics and detailed info on your ship.` and 26
records of ship description text. Tyrian 2000 grew that text to 40 records, six
of which read `Todo` and one `Toto`.

**A difficulty was added** in 2.1: `Lord of Game`, after Suicide.

**The high score ranks were reshuffled** in 2000. After Unranked, Tyrian 1.1 and
2.1 rank you Wimp, Easy, Normal, Hard, Impossible, Insanity, Suicide, Maniacal,
Zinglon, Nortaneous. Tyrian 2000 drops Wimp and Nortaneous, adds Terror and
Master, and ends on Zinglon: Easy, Normal, Hard, Impossible, Insanity, Suicide,
Maniacal, Terror, Master, Zinglon.

**A game mode was added** in 2000: `1 Player Timed Battle`, inserted before 2
Player Arcade in the player-count menu.

**Mouse configuration** shows up all over 2000: a Mouse row in the Options menu,
a Mouse row in the network options, a whole mouse settings group, and help text
for configuring and resetting mouse buttons. Mouse input itself was not new;
`Mouse` is already one of the three input devices named in 1.1 and 2.1.

**Twiddle names**, the code words for the special manoeuvres, went from
STEALTH, STORMWIND, TECHNO, ENEMY, WEIRD, UNKNOWN, NORTSHIPZ, DESTRUCT, ENGAGE to
the same list with **LIZARD** and **PRETZEL** inserted before DESTRUCT in 2000.

**Seventeen text groups are new in 2000:** ten groups of setup menu text, mouse
settings, licensing and ordering information, default high score and team names,
Super Tyrian text, and the Timed Battle planet list. Tyrian 1.1 and 2.1 handle
setup with separate DOS programs.

Other wording changes:

- The event banner `Unexplained speed increase!` became `Afterburners
  activated!` in 2.1.
- 2.1 added the event banners `** Danger! **` and `>>> Bonus Level <<<`, the
  `TIME REMAINING` and `>> Bonus Game <<` labels, and the `SUPER` tag.
- 2.1 added the keyboard shortcut hints `(Shortcut is ALT-L)` and
  `(Shortcut is ALT-S)` to the load and save help.
- 2.1 added the planet name `Skip It`.
- 2000 added the save prompts `Are you sure you want to save?` and
  `Original save:`, the `[/] Rear Weapon Mode` hint, `POWER:`, `Time Bonus` and
  `Lives Bonus`.
- 2000 added the network message `Other player stopped responding.`
- The ordering help line lost its phone number in 2000. Tyrian 1.1 and 2.1 tell
  you to `dial 1-800-972-7434 to order directly from Epic`.

## Datacubes

The story-text counts changed substantially in 2.1. They stayed fixed in 2000,
but the text itself changed.

| Episode | 1.1 | 2.1 | 2000 |
| --- | --- | --- | --- |
| 1 | 17 | 38 | 38 |
| 2 | 19 | 13 | 13 |
| 3 | 18 | 22 | 22 |
| 4 | | 68 | 68 |
| 5 | | | 13 |

Episode 1 more than doubled while episode 2 lost six cubes. That does not mean
the missing records were moved: exact comparison finds no nonblank 1.1
episode 2 cube copied verbatim into 2.1 episode 1. All four `cubetxt` files that
exist in both 2.1 and 2000 were edited for 2000, but their cube counts did not
move.

## Graphics

### Palettes

There are 23 palettes in 1.1 and 2.1 and 24 in 2000. Two of the shared ones were
repainted: palette 8 changed in 2.1 (120 of its 256 entries), and palette 20 was
replaced outright in 2000 (all 256 entries). Screen 4's indexed pixels did not
change in 2.1, but it uses palette 8 and therefore renders with a visibly darker
planet.

### Full-screen backdrops

`tyrian.pic` holds 12 screens in 1.1, 13 in 2.1 and 14 in 2000. Three of the
shared indexed images changed, and the changes are the most visible sign of the
game changing hands:

- **Screen 10, the publisher logo.** Tyrian 1.1 shows the Epic MegaGames logo.
  Tyrian 2.1 shows the same logo with `NOT LICENSED FOR COMMERCIAL DISTRIBUTION`
  stamped across the bottom. Tyrian 2000 replaces it with the **XSIV GAMES**
  logo.
- **Screen 2, the title backdrop.** The credit line under the planet reads
  `AN EPIC MEGAGAMES PRODUCTION ©1994` in 1.1 and 2.1. In 2000 the studio name
  is gone and the line reads `©1994-1999`.
- **Screen 6, the two-player interface frame**, was redrawn at both steps.

Screen 13 arrived in 2.1 and screen 14 in 2000, the latter being the fruit and
hexagons artwork for Hazudra Fodder.

### Sprite banks

`tyrian.shp` holds 11 banks in 1.1, 12 in 2.1 and 13 in 2000. Tyrian 1.1 stores
its seven sprite tables in a different order from the later releases:

| Bank | 1.1 | 2.1 and 2000 |
| --- | --- | --- |
| 0 | Planets | Large font |
| 1 | Large font | Small font |
| 2 | Small font | Tiny font |
| 3 | Faces | Planets |
| 4 | Options and help | Faces |
| 5 | Tiny font | Options and help |
| 6 | Weapons | Weapons |

The compiled banks come after that. Tyrian 2.1 added a second player-shot sheet,
and Tyrian 2000 added the sheet for its new ships. Three banks grew in 2000: the
planet and map sprites went from 151 to 152 frames, the cutscene faces from 12 to
18, and the menu art from 45 to 47.

The Christmas `tyrianc.shp` follows the same growth. It has 12 banks in 2.1 and
13 in 2000, gains the Tyrian 2000 ship sheet, and grows the same three array
banks. It is not byte-for-byte unchanged between those releases.

### Enemy sprite files

`newsh?.shp` files went 32, 34, 39. Tyrian 2.1 dropped `newshq.shp` and
`newsh@.shp` and added `newsh3.shp`, `newsh5.shp`, `newsh9.shp` and
`newsh^.shp`. Tyrian 2000 brought `newsh@.shp` back and added `newsh$.shp`,
`newsh%.shp`, `newsh'.shp` and `newsh(.shp`. Every one of these files holds
exactly 304 frame slots in every release.

Between 2.1 and 2000, 24 of the 34 shared enemy sheets are byte for byte
identical.

### Tilesets

Tyrian 1.1 ships four tilesets and 2.1 added `shapes).dat`, which is where ten of
episode 4's levels get their scenery. The number of tiles actually drawn is:

| Tileset | 1.1 | 2.1 | 2000 |
| --- | --- | --- | --- |
| `shapes).dat` | | 175 | 175 |
| `shapesw.dat` | 282 | 255 | 255 |
| `shapesx.dat` | 317 | 322 | 322 |
| `shapesy.dat` | 187 | 226 | 306 |
| `shapesz.dat` | 362 | 361 | 473 |

`shapesw.dat` lost 27 tiles in 2.1. `shapesz.dat`, used by 37 of Tyrian 2000's 70
levels, gained 112 tiles in that release after losing one in 2.1.

## Sound and music

**Music.** Tyrian 1.1 ships 34 songs. Tyrian 2.1 added seven: One Mustn't Fall,
Sarah's Song, A Field for Mag, Rock Garden, Quest for Peace, Composition in Q and
BEER. Tyrian 2000's `music.mus` is byte-identical to the 2.1 file.

**Sound effects.** Tyrian 1.1 and 2.1 both hold 29 effects, but five of them were
re-recorded in 2.1: SCALEDN2, PASS3, LAZB, HYPERD2 and SCALEDN1. Tyrian 2000
appended two more, MARS3 and NEEDLE2.

**Voices.** `voices.snd` is byte for byte identical in all three releases, all
nine samples. The Christmas voice bank `voicesc.snd` and the Christmas sprite
file `tyrianc.shp` both appear in 2.1. The voice bank is unchanged in 2000; the
sprite file is not, as described above.

## Demos

The five attract-mode demos were completely replaced in 2.1. Tyrian 1.1 plays
SAVARA, ASTEROID1, ASTEROID2, DELIANI and BUBBLES; from 2.1 onward it plays
TYRIAN, SAVARA, SOH JIN, MINES and DELIANI. All five files are byte for byte
identical between 2.1 and 2000.

## The files themselves

Only **six files keep both their name and their bytes across all three
releases**: `dpmi16bi.ovl`, `estsc.shp`, `rtm.exe`, `setup.box`, `tyrend.anm`
and `voices.snd`. The ending animation and the ending sprite art never changed.

Two more survived unchanged under new names. The DOS launcher stub `tyrian.exe`
became `tyrian2k.exe` in 2000, and `file0002.exe` became `tyrian2.exe`. Both are
byte for byte what shipped in 1995.

Of the 90 files that 1.1 and 2.1 share, 64 changed. Of the 80 that 2.1 and 2000
share, only 35 did.

Filename case changed wholesale: Tyrian 1.1 ships its data names in uppercase,
while the 2.1 and 2000 directories use lowercase. The dump normalizes all three
to lowercase so the trees remain directly comparable.

**Added in 2.1:** `cubetxt4.dat`, `levels4.dat`, `tyrian4.lvl`, `shapes).dat`,
`newsh3.shp`, `newsh5.shp`, `newsh9.shp`, `newsh^.shp`, `tyrianc.shp`,
`voicesc.snd`, `tshp2.pcx`, `shipedit.pcx`, `shipedit.exe`, `shipedit.doc`,
`user1.shp`, `user2.shp`.

**Removed in 2.1:** `newshq.shp`, `newsh@.shp`, `order_de.doc`, `order_uk.doc`.
The German and UK ordering sheets are 1.1 only.

**Added in 2000:** `cubetxt5.dat`, `levels5.dat`, `tyrian5.lvl`, `estpa.shp`,
`newsh$.shp`, `newsh%.shp`, `newsh'.shp`, `newsh(.shp`, `newsh@.shp` (back
again), four Windows icons and `readme.txt`.

The directory also contains this port's runtime: `opentyrian2000.exe`, `gm.sf2`,
ten DLLs, three `.pif` shortcuts, `opentyrian.cfg`, `tyrian.cfg`, and
`tyrian.sav`. The two renamed DOS binaries described above account for the
remaining additions.

**Removed in 2000:** most of the DOS distribution and support files:

- `netarena.exe`, `netipx.exe`, `netmodem.exe`, `netterm.exe`, `netterm.int`,
  and their five PCX screens;
- `order.exe`, `order.tfp`, and `order.doc`;
- `manual.doc`, `helpme.doc`, `license.doc`, `modems.txt`, `shipedit.doc`, and
  `file_id.diz`;
- `setup.ini`, `setup.int`, `helpme.exe`, `tyrset.pcx`, and `tyrian.ico`.

`shipedit.exe`, `dpmi16bi.ovl`, `rtm.exe`, and the old launchers remain.

The DOS exit screen also changed publisher. Tyrian 1.1 and 2.1 print a thank-you
box with instructions for reaching Epic MegaGames on CompuServe. Tyrian 2000
retitles it `Tyrian 2000`, blanks the upper message area, and replaces the bottom
ordering panel with XSIV Games' postal, telephone, and web details.

The credits follow the same split. Tyrian 1.1 and 2.1 share an identical
`tyrian.cdt`; 2000 adds artwork and installer credits, removes the Epic thank-you
block, and replaces the "Team Beta" line with a new betatester list.

## Container format changes

Five things changed in the containers themselves. Data readers must handle all
five.

**1. Where the item tables live.** Tyrian 1.1 keeps a complete set of item and
enemy tables at the end of every `tyrian?.lvl` file, and `tyrian.hdt` holds text
only.

Tyrian 2.1 moved the episode 1-3 set into `tyrian.hdt`, behind the text, and put
a four-byte offset at the front of the file pointing at it. Episode 4 still
keeps its own set at the end of `tyrian4.lvl`. Tyrian 2000 followed the same
rule for episode 5.

**2. The stored table sizes.** Every item block starts with seven counts, and
they identify the release on their own:

| Release | Weapons | Ports | Generators | Ships | Sidekicks | Shields | Enemies |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1.1 | 720 | 39 | 6 | 12 | 18 | 10 | 850 |
| 2.1 | 780 | 42 | 6 | 13 | 30 | 10 | 850 |
| 2000 | 818 | 60 | 6 | 18 | 37 | 11 | 850 |

The number of specials is not in that header. It is 25, 46 and 54.

**3. Second banks.** Tyrian 2000 is the only release with a gap: weapons 819 to
999 and enemies 851 to 1000 do not exist, and the tables resume at 1000 and
1001. Each of its three item blocks also has 77 bytes after the declared tables:
one extra duplicate enemy record with no graphics, which the game never loads.

**4. The compiled sprite terminator.** Tyrian 2.1 introduced the `0x0f` byte that
ends a compiled 12-pixel frame. Tyrian 1.1 has no terminator: a
frame runs until the next offset in the sheet's own table, and the streams are
padded with zero bytes that skip nothing. A decoder written for 2.1 will read
straight through a 1.1 frame into the next one.

**5. The `tyrian.shp` bank order.** Tyrian 1.1 stores its seven sprite tables in
the order given under Sprite banks above, so a reader that assumes the 2.1 layout
draws the font where it expects planets.

The existing entries in the shapebank table did **not** change. This table maps
an enemy's 1-based bank number to the file holding its frames; every release
stores it in `file0001.exe`, and later releases only append entries:

```
1.1   2478ABCDEFGHIJKLMNOPQRSTU5#V0@
2.1   2478ABCDEFGHIJKLMNOPQRSTU5#V0@3^59
2000  2478ABCDEFGHIJKLMNOPQRSTU5#V0@3^59'%
```

No slot was ever reassigned, so bank 3 means `newsh7.shp` (and bank 7 means
`newshc.shp`) in all three releases.

Tyrian 2.1 added these level-script commands:

- `]g` GALAGA, `]e` ENGAGE, `]x` extra game, and `]2` two-player section jump;
- `]w` Stalker 21.126's TIME WAR gate, `]t` timer jump, and `]l` dead-player jump;
- `]s` savepoint, `]b` backup save slot, and `]i` shop music;
- `]n` clear escape, `]h` difficulty gate, and `]+` extra datacube capacity.

Tyrian 1.1 alone uses `]S` to synchronize network text. Tyrian 2000 added `]T`
and `]q` for Timed Battle.

## Curiosities

- **`newsh~.shp` ships in all three releases but is not an enemy bank.** The
  shapebank table has no `~` entry in any release, so no level enemy-bank event
  can ask for it.
- **Bank 26 points at a file Tyrian 1.1 does not have.** The table says
  `newsh5.shp`, which first ships in 2.1. No level asks for bank 26 in any
  release, so it never mattered.
- **`newshq.shp` is 1.1 only, and 1.1 never loads it.** Bank 21 is `Q` in all
  three tables and no level in any release uses it.
- **`newsh@.shp` left and came back.** Tyrian 1.1 loads it as bank 30, 2.1 drops
  the file and stops using the bank, and 2000 ships the file again without any
  level loading it.
- **Tyrian 2000's `newsh(.shp` also has no enemy-bank entry.** `newsh$.shp` is
  absent from that table too, but it has a separate purpose: the custom-ship
  loader opens it directly by name.
- **Thirteen weapon ports in Tyrian 2000 are named `Test`**, and six specials and
  five sidekicks are named `None`.
- **Ship descriptions were never finished.** Six of the ship info records in
  Tyrian 2000 read `Todo` and one reads `Toto`.
- **`Advanced MircoFusion`** is misspelled in the generator table of all three
  releases.
- **Tyrian 1.1's fourth episode was called `Electronic Arts`** in the episode
  menu, a year before it shipped as An End to Fate.
- **The DOS ship editor shipped with 2.1 and 2000** as `shipedit.exe`, along with
  two sample ships in `user1.shp` and `user2.shp` that the game itself never
  reads. The 2.1 documentation calls the editor a `pre-release version`.
- **The Christmas theme predates Tyrian 2000.** Both the Christmas sprite file
  and the Christmas voice bank are already in 2.1.
