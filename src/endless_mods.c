/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Endless mode: the mutator table -- named sector themes, danger tiers and help text.
 *
 * One of the endless_*.c files that make up endless mode: endless.h is the public
 * interface, endless_internal.h the state and helpers the group shares.
 */

#include "endless.h"
#include "endless_internal.h"
#include "endless_levelprofile.h"  // GENERATED endlessLevelProfiles[] (per-level intrinsic danger)


const EndlessMod endlessModTable[] = {
	// -- hostile --  (`word` is the Chart-a-Course monitor label: a plain WHAT-IT-DOES phrase,
	//                 not flavor -- "more enemy HP", not "tough hulls". Keep each under ~105px
	//                 in TINY_FONT (roughly 20 chars) so it fits the monitor's 113px columns;
	//                 game_menu.c anchors threats top-left and boons bottom-right)
	{ ENDLESS_MOD_FORTIFIED,     10, "more enemy HP" },
	{ ENDLESS_MOD_FRENZY,        10, "faster enemy fire" },
	{ ENDLESS_MOD_SWIFT,          8, "faster enemy shots" },
	{ ENDLESS_MOD_DEVASTATING,   10, "harder enemy hits" },
	{ ENDLESS_MOD_ENRAGE,        10, "enemy fire rate climbs" },
	{ ENDLESS_MOD_GRAVITY,        8, "downward pull" },
	{ ENDLESS_MOD_ELITEPACK,     20, "half enemies elite" },
	{ ENDLESS_MOD_OVERCLOCK,     16, "faster enemy attacks" },  // THREE effects at once: fire rate (-30%) + shot speed (+40%) + Slipstream-level scroll (70%). Weighted above plain Slipstream/Frenzy for that stack (the monitor adds a separate scroll row in endlessCourseModRows so "+ fire" ambiguity never returns)
	{ ENDLESS_MOD_SLIPSTREAM,     6, "faster scrolling" },      // the level rushes at you -- less reaction time
	{ ENDLESS_MOD_KAMIKAZE,      12, "enemies home in" },   // moderate homing, NO ram -- the mid sector tier (what Homing used to be)
	{ ENDLESS_MOD_HOMING,         6, "light homing" },      // the gentlest homing tier -- enemies barely lean toward you
	{ ENDLESS_MOD_RAMPAGE,       50, "enemies ram you" },   // gamble-only brutal Kamikaze: strong homing + extra ram damage (top-tier danger weight)
	{ ENDLESS_MOD_OVERLOAD,      30, "extreme enemy attacks" },  // Overclock cranked way up: WARP-level scroll (220%, same as Warp) PLUS heavy fire (-55%) and shot speed (+90%). Must outrank Warp's scroll-only 20 -- it does everything Warp does and more
	{ ENDLESS_MOD_APEX,          40, "all enemies elite" },
	{ ENDLESS_MOD_LEGION,        50, "all champion enemies" },
	{ ENDLESS_MOD_WARP,          20, "much faster scrolling" },  // Slipstream cranked way up (rare injected)
	{ ENDLESS_MOD_BACKFIRE, 12, "kills jam your guns" },
	{ ENDLESS_MOD_BURNOUT,   18, "kills weaken guns" },
	{ ENDLESS_MOD_MISFIRE,   14, "kills cut your damage" },
	{ ENDLESS_MOD_OVERHEAT,  14, "hull burns over time" },  // the reactor cooks you (gamble deal + rare Redline sector)
	{ ENDLESS_MOD_TOPSY,     10, "upside-down view" },      // the playfield flips; controls invert with it (boss-style) -- a pure disorientation tax
	{ ENDLESS_MOD_SLUGGISH,  15, "your ship slowed" },      // ship + mouse/touch crawl -- half the reach to dodge with
	{ ENDLESS_MOD_SHIELDLESS, 12, "no shield regen" },      // shields never recharge -- once spent, you fly on armor
	{ ENDLESS_MOD_DEADGEN,   30, "generator dead" },        // no shield regen AND the main gun is starved of power (super-rare)
	{ ENDLESS_MOD_MARTYRDOM, 18, "kills fire a burst" },    // a slain enemy's death throe: a radial burst (4/6/8 by tier)
	{ ENDLESS_MOD_SEEKER,    14, "shots curve at you" },    // enemy projectiles bend once toward you mid-flight
	{ ENDLESS_MOD_STATIC,    11, "hits drain power" },      // taking damage bleeds the generator -- mistakes throttle your guns
	{ ENDLESS_MOD_RETALIATION, 15, "kills quicken fire" },  // a kill spree whips enemy fire faster (distinct from time-based Enrage)
	// The 100th-zone finale marker. A NULL word means "no monitor row and no help-line phrase": it is
	// a label, not a mechanic, so the threat list stays purely the sector's real dangers. The reward
	// IS the finale bounty (roughly 15x the base clear on its own), and since the danger score sums
	// the same table it also guarantees the sector outranks everything else on the slate.
	{ ENDLESS_MOD_THEEND,   150, NULL },
	// -- boons: they HELP you, so little/no cash (a couple pay big instead) --
	{ ENDLESS_MOD_FRAGILE,       -5, "less enemy HP" },
	{ ENDLESS_MOD_TURBODRIVE,      0, "kills quicken guns" },
	{ ENDLESS_MOD_OVERCHARGE,     0, "more weapon damage" },
	{ ENDLESS_MOD_DILATION,       0, "slower enemy shots" },
	{ ENDLESS_MOD_FAVOR,          0, "cheaper next shop" },
	{ ENDLESS_MOD_OVERDRIVE,   0, "kills stack firepower" },
	{ ENDLESS_MOD_OVERBLAST,   0, "kills stack damage" },
	{ ENDLESS_MOD_BOUNTY,        30, "big cash payout" },
	{ ENDLESS_MOD_CURSED,        40, "cash now, empty shop" },
	{ ENDLESS_MOD_NOCHAMP,        0, "no champion enemies" },     // no clear-cash: the boon already costs you the elite/champion bounties
	{ ENDLESS_MOD_NOELITE,        0, "no elites or champions" },  // (same -- these thin the very enemies that pay the fat bounties)
	// The ten later boons. Most carry a NEGATIVE reward: the sector is genuinely easier to fly, so it
	// pays LESS than a clean one -- the mirror of a hostile bit's positive reward. The two that change
	// nothing about the fight itself (Star Charts, Auxiliary Reactor) sit at 0, and Breakthrough is the
	// deepest cut of all because a whole extra perk dwarfs one sector's cash.
	{ ENDLESS_MOD_AEGIS,         -5, "shield blocks overflow" },
	{ ENDLESS_MOD_FLAKSCREEN,    -5, "fewer added shots" },
	{ ENDLESS_MOD_AUXREACTOR,     0, "free shield recharge" },
	{ ENDLESS_MOD_LOWPROFILE,    -8, "smaller hitbox" },
	{ ENDLESS_MOD_GIANTKILLER,   -6, "weaker elite armor" },
	{ ENDLESS_MOD_SHOCKWAVE,     -4, "elite kills wipe shots" },
	{ ENDLESS_MOD_STARCHARTS,     0, "more routes next" },
	{ ENDLESS_MOD_BREAKTHROUGH, -10, "perk on clear" },
	{ ENDLESS_MOD_SOFTLANDING,   -4, "safer collisions" },
	{ ENDLESS_MOD_CLEANSIGNALS,  -5, "weaker elite attacks" },
};

const EndlessTheme endlessHostileThemes[] = {
	// -- single dangers --
	{ ENDLESS_MOD_FORTIFIED,   "Fortified" },
	{ ENDLESS_MOD_FRENZY,      "Frenzy" },
	{ ENDLESS_MOD_SWIFT,       "Swift Death" },
	{ ENDLESS_MOD_DEVASTATING, "Devastating" },
	{ ENDLESS_MOD_ENRAGE,      "Enrage" },
	{ ENDLESS_MOD_GRAVITY,     "Gravity Well" },
	{ ENDLESS_MOD_ELITEPACK,   "Elite Pack" },
	{ ENDLESS_MOD_OVERCLOCK,   "Overclock" },
	{ ENDLESS_MOD_SLIPSTREAM,  "Slipstream" },    // faster scroll: the level rushes at you (a threat, not the old boon)
	{ ENDLESS_MOD_TOPSY,       "Topsy Turvy" },  // fork: upside-down screen (boss-style -- controls invert with the view)
	{ ENDLESS_MOD_SLUGGISH,    "Molasses" },      // fork: slowed ship (depth-scaled; combos below -- its gravity pairing is a rare injection)

	// -- doubles --
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY,       "Onslaught" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT,        "Juggernaut" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING,  "Siege" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,           "Barrage" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING,     "Fusillade" },
	{ ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,      "Piercing Storm" },
	{ ENDLESS_MOD_ENRAGE | ENDLESS_MOD_FORTIFIED,       "War of Attrition" },
	{ ENDLESS_MOD_ENRAGE | ENDLESS_MOD_SWIFT,           "Escalation" },
	{ ENDLESS_MOD_ENRAGE | ENDLESS_MOD_DEVASTATING,     "Wrath" },
	{ ENDLESS_MOD_GRAVITY | ENDLESS_MOD_FORTIFIED,      "Dense Matter" },
	{ ENDLESS_MOD_GRAVITY | ENDLESS_MOD_SWIFT,          "Event Horizon" },
	{ ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING,    "Crushing Weight" },
	{ ENDLESS_MOD_GRAVITY | ENDLESS_MOD_FRENZY,         "Whirlpool" },
	{ ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FORTIFIED,    "Praetorians" },
	{ ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_SWIFT,        "Vanguard" },
	{ ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_DEVASTATING,  "Elite Guard" },
	{ ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FRENZY,       "Warband" },
	{ ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_FORTIFIED,    "Meltdown" },
	{ ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_DEVASTATING,  "Reactor Breach" },
	{ ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_GRAVITY,      "Riptide" },  // not the Overdrive buff -- renamed to avoid the clash with the OVERDRIVE boon below
	{ ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_ELITEPACK,    "Prototype Swarm" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_FRENZY,      "Fast Lane" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_DEVASTATING, "Runaway" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_FORTIFIED,   "Bypass" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_ENRAGE,          "Bloodrage" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_OVERCLOCK,       "Redline" },
	{ ENDLESS_MOD_SWIFT | ENDLESS_MOD_OVERCLOCK,        "Hypervelocity" },
	{ ENDLESS_MOD_ENRAGE | ENDLESS_MOD_GRAVITY,         "Accretion" },
	{ ENDLESS_MOD_ENRAGE | ENDLESS_MOD_ELITEPACK,       "Elite Fury" },
	{ ENDLESS_MOD_ENRAGE | ENDLESS_MOD_OVERCLOCK,       "Overburn" },
	{ ENDLESS_MOD_GRAVITY | ENDLESS_MOD_ELITEPACK,      "Neutron Star" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_DEVASTATING,    "Glass Cannon" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_FORTIFIED,		"Capsize" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_FRENZY,			"Vertigo" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_SWIFT,			"Corkscrew" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_DEVASTATING,		"Upheaval" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_ENRAGE,			"Whirligig" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_ELITEPACK,		"Off Kilter" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_OVERCLOCK,		"Barrel Roll" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_GRAVITY,			"Somersault" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_SLUGGISH,			"Head Rush" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_FORTIFIED,		"Anchored" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_FRENZY,		"Slog" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_SWIFT,			"Bogged Down" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_DEVASTATING,	"Lead Boots" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_ENRAGE,		"War of Attrition II" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_ELITEPACK,		"Ballast" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_OVERCLOCK,		"Millstone" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_FORTIFIED,   "Attrition" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_FRENZY,      "Exposed" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_SWIFT,       "Pincushion" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_DEVASTATING, "Glass Hull" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_ENRAGE,      "Overexposed" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_ELITEPACK,   "Outmatched" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_OVERCLOCK,   "No Cover" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_GRAVITY,     "Naked Descent" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_TOPSY,       "Spin Out" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_SLUGGISH,    "Sitting Target" },

	// -- triples --
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,       "Nightmare" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,     "Maelstrom" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,  "Fortress Guns" },
	{ ENDLESS_MOD_ENRAGE | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,     "Deadly Escalation" },
	{ ENDLESS_MOD_GRAVITY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,    "Singularity" },
	{ ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Elite Siege" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING,    "Blitzkrieg" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING,   "Colossus" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_DEVASTATING,    "Siege Engine" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_GRAVITY,        "Heavy Siege" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_ENRAGE,         "Bloodwall" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING,      "Vortex" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_DEVASTATING,       "Firestorm" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_GRAVITY,            "Cyclone" },
	{ ENDLESS_MOD_SWIFT | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_GRAVITY,            "Death Spiral" },
	{ ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,     "Death Squad" },
	{ ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING,    "War Machine" },
	{ ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING,   "Doomguard" },
	{ ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_DEVASTATING,    "Elite Wrath" },
	{ ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,     "Overkill" },
	{ ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING,    "Full Auto" },
	{ ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Iron Storm" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_ELITEPACK, "Bloodtide" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_OVERCLOCK, "Warpath" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT | ENDLESS_MOD_ENRAGE, "Ironclad" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT | ENDLESS_MOD_GRAVITY, "Rampart" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT | ENDLESS_MOD_ELITEPACK, "Hailstorm" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT | ENDLESS_MOD_OVERCLOCK, "Thunderclap" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_GRAVITY, "Pandemonium" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_ELITEPACK, "Requiem" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_OVERCLOCK, "Damnation" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_ELITEPACK, "Perdition" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_OVERCLOCK, "Warhead" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_OVERCLOCK, "Sledgehammer" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_ENRAGE, "Warmonger" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_ELITEPACK, "Devastator" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_OVERCLOCK, "Ravager" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_GRAVITY, "Desolation" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_ELITEPACK, "Bombardment" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_OVERCLOCK, "Storm Surge" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_ELITEPACK, "Iron Rain" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_OVERCLOCK, "Steel Storm" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_OVERCLOCK, "Death March" },
	{ ENDLESS_MOD_SWIFT | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_ELITEPACK, "Hellfire" },
	{ ENDLESS_MOD_SWIFT | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_OVERCLOCK, "Brimstone" },
	{ ENDLESS_MOD_SWIFT | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_ELITEPACK, "Wildfire" },
	{ ENDLESS_MOD_SWIFT | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_OVERCLOCK, "Firewall" },
	{ ENDLESS_MOD_SWIFT | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_OVERCLOCK, "Overrun" },
	{ ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_GRAVITY, "Stampede" },
	{ ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_OVERCLOCK, "Warzone" },
	{ ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_OVERCLOCK, "Killzone" },
	{ ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_OVERCLOCK, "Crossfire" },
	{ ENDLESS_MOD_ENRAGE | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_ELITEPACK, "Massacre" },
	{ ENDLESS_MOD_ENRAGE | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_OVERCLOCK, "Butchery" },
	{ ENDLESS_MOD_ENRAGE | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_OVERCLOCK, "Bloodbath" },
	{ ENDLESS_MOD_GRAVITY | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_OVERCLOCK, "Slaughter" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,          "Tumbler" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Keelhaul" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING,   "Free Fall" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_FRENZY,       "Disorient" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,       "Bullet Wade" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Deadlock" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_SWIFT | ENDLESS_MOD_ELITEPACK,    "Molasses Run" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,        "Kill Box" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Last Ditch" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_SWIFT,     "No Quarter" },

	// -- quads --
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING, "Cataclysm" },
	{ ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,   "Elite Nightmare" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_GRAVITY,     "Abaddon" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE, "Armageddon" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE,    "Apocalypse" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_ENRAGE,      "Berserker Rush" },
	{ ENDLESS_MOD_GRAVITY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE,   "Event Collapse" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE, "Black Star" },
	{ ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING, "Praetorian Guard" },
	{ ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,  "Elite Storm" },
	{ ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,  "Bullet Hell" },
	{ ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING, "Overlord" },

	// -- Static Discharge (COMMON: also in the combinable widen pool, so it mixes freely) --
	{ ENDLESS_MOD_STATIC,                            "Static Discharge" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_FRENZY,       "Power Leech" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_DEVASTATING,  "Short Circuit" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_SWIFT,        "Live Wire" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_FORTIFIED,    "Grounded" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_ELITEPACK,    "Feedback" },

	// -- Retaliation (UNCOMMON: in the shuffle pool but NOT the combinable widen, like Molasses) --
	{ ENDLESS_MOD_RETALIATION,                            "Retaliation" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_FRENZY,       "Backlash" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_SWIFT,        "Payback" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_DEVASTATING,  "Riposte" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_ENRAGE,       "Bad Blood" },   // the +5 Retaliation+Enrage synergy sector
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_FORTIFIED,    "Grudge" },

	// -- the under-named pairings. The four reactive dangers (Martyrdom / Seeker / Static /
	//    Retaliation), Slipstream and the two ship handicaps only ever had a handful of rows each, so
	//    most of their pairings fell through to a generic word. Every remaining pair among the
	//    signature-drawable bits is named here, EXCEPT two deliberate holes: Overclock+Slipstream
	//    (Overclock already carries the same scroll -- a redundant bit) and Gravity+Sluggish (that is
	//    Tar Pit, and it must stay in the rare-injection pool rather than become an ordinary theme). --
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_FRENZY,         "Dying Breath" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_ENRAGE,         "Death Throes" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_GRAVITY,        "Deadfall" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_OVERCLOCK,      "Powder Trail" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_TOPSY,          "Death Roll" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_STATIC,         "Death Spark" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_SHIELDLESS,     "Shrapnel" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_RETALIATION,    "Vicious Cycle" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_SEEKER,         "Dead Aim" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_SLIPSTREAM,     "Drive-By" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_SLUGGISH,       "No Escape" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_ENRAGE,            "Guided Fury" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_GRAVITY,           "Curveball" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_OVERCLOCK,         "Fast Track" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_ELITEPACK,         "Marked Man" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_TOPSY,             "Twisted Aim" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_STATIC,            "Live Rounds" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_SHIELDLESS,        "Hunted" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_RETALIATION,       "Hot Pursuit" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_SLIPSTREAM,        "Quick Lock" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_SLUGGISH,          "Easy Mark" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_ENRAGE,            "Rising Static" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_GRAVITY,           "Ion Well" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_OVERCLOCK,         "Overdraft" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_TOPSY,             "Short Out" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_SHIELDLESS,        "Bare Wires" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_RETALIATION,       "Backcurrent" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_SLIPSTREAM,        "Third Rail" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_SLUGGISH,          "Power Sap" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_GRAVITY,      "Sinking Feeling" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_OVERCLOCK,    "Chain Fire" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_ELITEPACK,    "Command Chain" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_TOPSY,        "Backspin" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_SHIELDLESS,   "Open Wound" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_SLIPSTREAM,   "Hot Lap" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_SLUGGISH,     "Treadmill" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_SWIFT,         "Slipshot" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_ENRAGE,        "Fast Forward" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_GRAVITY,       "Downdraft" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_ELITEPACK,     "Fast Company" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_TOPSY,         "Overturn" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_SHIELDLESS,    "Run for It" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_SLUGGISH,      "Falling Behind" },

	// -- triples built around the same under-named bits. The widen pool routinely draws 3 of these, so
	//    without a row each they all read as a generic ominous word. No Gravity+Sluggish row here either. --
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,            "Arc Storm" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING,   "Iron Circuit" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FRENZY,        "Overcurrent" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING,     "Magnetar" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_FRENZY,           "Rolling Blackout" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_DEVASTATING,  "Total Loss" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_TOPSY | ENDLESS_MOD_FRENZY,            "Scrambled" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,            "Needle Storm" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING,   "Guided Siege" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_SWIFT,         "Elite Trackers" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING,     "Homing Well" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_FRENZY,           "Hunter's Rhythm" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_SWIFT,        "Nowhere to Hide" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_TOPSY | ENDLESS_MOD_DEVASTATING,       "Crooked Sky" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,         "Final Volley" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY,     "Hard to Kill" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_DEVASTATING, "Funeral Pyre" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_FRENZY,       "Falling Ashes" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_DEVASTATING,   "Scorched Earth" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_SWIFT,     "Splinters" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_TOPSY | ENDLESS_MOD_FRENZY,         "Death Blossom" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,       "Answering Fire" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Blood Feud" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_SWIFT,    "Honor Guard" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING, "Rebound" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_SWIFT,       "Boiling Over" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_FRENZY,  "Raw Nerve" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_TOPSY | ENDLESS_MOD_SWIFT,        "Whiplash" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_SWIFT,          "Inverted Guard" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_DEVASTATING,       "Head Over Heels" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_FRENZY,           "Downside Up" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_DEVASTATING,   "Exposed Flank" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_FRENZY,      "Bottoming Out" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_SWIFT,        "Bleeding Out" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_DEVASTATING, "Thin Skinned" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,   "Head-On" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FRENZY,    "Fast Movers" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING, "Steep Descent" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_SWIFT,        "Full Throttle" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_TOPSY | ENDLESS_MOD_DEVASTATING,     "Dead Slow" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_SWIFT,      "Easy Pickings" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_DEVASTATING,    "Long Haul" },

	// -- quads: the widen's hard ceiling is four bits, so these are the busiest ordinary sectors there are --
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,          "Grid Lock" },
	{ ENDLESS_MOD_STATIC | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Dead Battery" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,          "Smart Siege" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING,    "Target Locked" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,     "Dying Light" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT,    "Martyr Guard" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,   "Even the Score" },
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY, "Blood Oath" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_ENRAGE,              "Spin Cycle" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,     "Upended" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,    "Cut to the Bone" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,      "Slow Death" },
};

// KAMIKAZE sectors (homing rammers) are much harder to fly, so they get their own pool and are
// injected RARELY (see endlessGenerateCourses) instead of shuffled in with the normal hostiles.
const EndlessTheme endlessKamikazeThemes[] = {
	{ ENDLESS_MOD_KAMIKAZE,                              "Kamikaze" },
	{ ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_FORTIFIED,      "Battering Ram" },
	{ ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_FRENZY,         "Zealots" },
	{ ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_SWIFT,          "Dive Bombers" },
	{ ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_DEVASTATING,    "Suicide Run" },
	{ ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_GRAVITY,        "Black Hole" },
	{ ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_ELITEPACK,      "Berserkers" },
	{ ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING, "Kamikaze Storm" },
	{ ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_ENRAGE,        "Fanatics" },
	{ ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_OVERCLOCK,     "Rocket Swarm" },
	{ ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING,    "Death Charge" },
	{ ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Siege Ram" },
};

// HOMING sectors are the MILD cousin of Kamikaze: enemies gently drift toward you (no ram bonus),
// just enough to be a nuisance. Their own pool, injected at a moderate rate (see endlessGenerateCourses)
// so you meet them regularly -- while the real Kamikaze pool above stays super rare.
const EndlessTheme endlessHomingThemes[] = {
	{ ENDLESS_MOD_HOMING,                              "Stalkers" },
	{ ENDLESS_MOD_HOMING | ENDLESS_MOD_FORTIFIED,      "Bloodhounds" },
	{ ENDLESS_MOD_HOMING | ENDLESS_MOD_FRENZY,         "Harriers" },
	{ ENDLESS_MOD_HOMING | ENDLESS_MOD_SWIFT,          "Pursuers" },
	{ ENDLESS_MOD_HOMING | ENDLESS_MOD_DEVASTATING,    "Predators" },
	{ ENDLESS_MOD_HOMING | ENDLESS_MOD_GRAVITY,        "Undertow" },
	{ ENDLESS_MOD_HOMING | ENDLESS_MOD_ELITEPACK,      "Wolfpack" },
	{ ENDLESS_MOD_HOMING | ENDLESS_MOD_ENRAGE,         "Bloodlust" },
};

const EndlessTheme endlessBoonThemes[] = {
	{ ENDLESS_MOD_FRAGILE,    "Fragile Foe" },
	{ ENDLESS_MOD_BOUNTY,     "Bounty Run" },
	{ ENDLESS_MOD_TURBODRIVE,  "Turbodrive" },   // the shop's Turbodrive buy sets this same bit (endlessTryBuyTurbodrive)
	{ ENDLESS_MOD_OVERDRIVE, "Overdrive" },   // the shop's Overdrive buy sets this same bit (endlessTryBuyOverdrive)
	{ ENDLESS_MOD_OVERBLAST, "Overblast" },   // the shop's Overblast buy sets this same bit (endlessTryBuyOverblast)
	{ ENDLESS_MOD_OVERCHARGE, "Overcharged" },
	{ ENDLESS_MOD_DILATION,   "Time Dilation" },
	{ ENDLESS_MOD_FAVOR,      "Merchant's Favor" },
	{ ENDLESS_MOD_CURSED,     "Cursed Bounty" },
	{ ENDLESS_MOD_NOCHAMP,    "Leaderless" },     // no champions -- the elite pack loses its purple overlords
	{ ENDLESS_MOD_NOELITE,    "Rank and File" },  // no elites or champions -- only ordinary troops (the stronger, rarer boon)
	// -- boon pairs (stack your buffs) --
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_OVERCHARGE, "Ascendant" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_OVERCHARGE,  "Bullet Time" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_TURBODRIVE,   "In the Zone" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_BOUNTY,       "Easy Money" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_BOUNTY,         "Windfall" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_TURBODRIVE,    "Blood Frenzy" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_OVERCHARGE,   "Executioner" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_FAVOR,        "Clearance" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_BOUNTY,     "Killing Spree" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_BOUNTY,    "Mercenary" },
	{ ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_OVERCHARGE, "Power Surge" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_OVERCHARGE, "Deadeye" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_DILATION,   "Sharpshooter" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_BOUNTY,     "Bounty Hunter" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_DILATION, "Killstreak" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_OVERDRIVE, "Bloodrush" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_CURSED, "Adrenaline" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_FAVOR, "Momentum" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_OVERDRIVE, "Berserk" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_CURSED, "Fortune" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_FAVOR, "Jackpot" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_CURSED, "Gold Rush" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_FAVOR, "Bonanza" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_BOUNTY, "Lucky Break" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_OVERDRIVE, "Fire Sale" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_CURSED, "Discount" },
	// -- no-elite-tier boon pairs. NOCHAMP gets the wider set (three), NOELITE fewer (two), one of the
	//    several levers keeping the stronger NOELITE the rarer sight. Never pair NOCHAMP with NOELITE
	//    (endlessEnforceEliteRules strips the redundant NOCHAMP if they ever meet). --
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_OVERCHARGE, "Purge" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_BOUNTY,     "Trophy Room" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_FRAGILE,    "Mop Up" },
	{ ENDLESS_MOD_NOELITE | ENDLESS_MOD_OVERCHARGE, "Marksman" },
	{ ENDLESS_MOD_NOELITE | ENDLESS_MOD_FAVOR,      "Clean Slate" },
	// -- boon triples --
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_OVERCHARGE, "Ascension" },
	// -- the later boons: each on a system the rows above never touch (the shield/armor boundary, the
	//    rising tide, the generator, the hitbox, elite STATS rather than the elite tier, enemy
	//    projectiles, the chart itself). BREAKTHROUGH is deliberately absent -- it lives in its own
	//    tiny pool below, so the ordinary boon deal and the Jackpot can never hand it out. --
	{ ENDLESS_MOD_AEGIS,        "Aegis Gate" },
	{ ENDLESS_MOD_FLAKSCREEN,   "Flak Screen" },
	{ ENDLESS_MOD_AUXREACTOR,   "Auxiliary Reactor" },
	{ ENDLESS_MOD_LOWPROFILE,   "Low Profile" },
	{ ENDLESS_MOD_GIANTKILLER,  "Giant Killer" },
	{ ENDLESS_MOD_SHOCKWAVE,    "Disruption Pulse" },   // not "Shockwave" -- that word is already a generated hostile name
	{ ENDLESS_MOD_STARCHARTS,   "Star Charts" },
	{ ENDLESS_MOD_SOFTLANDING,  "Soft Landing" },
	{ ENDLESS_MOD_CLEANSIGNALS, "Clean Signals" },
	// -- pairs with the older boons. Each pairs ACROSS systems (a shield boon beside a damage boon,
	//    a hitbox boon beside a shot-speed boon), so nothing overlaps into a wasted half. --
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_AUXREACTOR,        "Full Deflector" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_LOWPROFILE,        "Untouchable" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_DILATION,     "Needle Threader" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_OVERCHARGE,   "Pinpoint" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_OVERCHARGE,  "Titan Bane" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_BOUNTY,      "Trophy Cull" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_OVERCHARGE,    "Ion Burst" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_DILATION,     "Open Air" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_FRAGILE,      "Clear Corridor" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_FAVOR,        "Deep Survey" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_BOUNTY,       "Recon Data" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_FRAGILE,     "Cushioned" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_FRAGILE,    "Jamming Field" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_OVERCHARGE,   "Full Power" },
	// -- the remaining cross-system pairs among the boons endlessMakeBoonCombo can actually draw
	//    (Turbodrive and Overdrive are NOT in that pool, so their pairings stay as authored above).
	//    NOELITE deliberately gains nothing here: its short row count is one of the levers keeping the
	//    stronger no-elite-tier boon the rarer sight, while NOCHAMP may spread out. --
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_BOUNTY,            "Safe Haul" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_FAVOR,             "Insurance" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_SHOCKWAVE,         "Bulwark" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_GIANTKILLER,       "Bastion" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_STARCHARTS,        "Guarded Route" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_CLEANSIGNALS,      "Quiet Shield" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_SOFTLANDING,       "Padded Hull" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_FLAKSCREEN,        "Double Cover" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_FRAGILE,      "Wisp" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_BOUNTY,       "Ghost Run" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_FAVOR,        "Low Overhead" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_SHOCKWAVE,    "Pressure Drop" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_GIANTKILLER,  "Blind Spot" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_AUXREACTOR,   "Efficient" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_STARCHARTS,   "Scout Profile" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_FRAGILE,      "Full Reserves" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_BOUNTY,       "Overflow" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_FAVOR,        "Surplus" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_DILATION,     "Cool Head" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_SHOCKWAVE,    "Capacitor" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_GIANTKILLER,  "Amped" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_CLEANSIGNALS, "Steady State" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_BOUNTY,      "Safe Delivery" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_OVERCHARGE,  "Reinforced" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_DILATION,    "Easy Does It" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_LOWPROFILE,  "Nimble" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_GIANTKILLER, "Rough and Ready" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_FRAGILE,       "Clean Sweep" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_BOUNTY,        "Shock Value" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_DILATION,      "Breathing Room" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_GIANTKILLER,   "Kingslayer" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_FRAGILE,      "Easy Survey" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_DILATION,     "Long View" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_OVERCHARGE,   "Bright Prospect" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_LOWPROFILE,   "Silent Survey" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_BOUNTY,       "Umbrella" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_FAVOR,        "Cheap Cover" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_OVERCHARGE,   "Counterbattery" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_FRAGILE,     "Paper Tigers" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_FAVOR,       "Bargain Hunt" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_OVERCHARGE, "Upper Hand" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_BOUNTY,     "Soft Targets" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_DILATION,   "Dead Calm" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_LOWPROFILE, "Off the Grid" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_DILATION,        "No Brass" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_FAVOR,           "Vacancy" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_LOWPROFILE,      "Unnoticed" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_GIANTKILLER,     "Toppled" },
	// -- boon triples: endlessMakeBoonCombo rolls a third bit ~40% of the time, so these are common
	//    enough to deserve names of their own (the table held exactly one before) --
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_DILATION,      "Untouched" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_FRAGILE,       "Safe Conduct" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_AUXREACTOR,   "Cocoon" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_FRAGILE,   "Clean Escape" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_DILATION, "Ghost Corridor" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_BOUNTY | ENDLESS_MOD_FAVOR,        "Golden Run" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_BOUNTY | ENDLESS_MOD_FAVOR,        "Prospector" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_OVERCHARGE, "Paper Crown" },
};

// BREAKTHROUGH: clearing the sector owes a bonus perk pick -- by far the strongest thing a single
// course can hand out, so it is the rarest boon in the game. Its own tiny pool, drawn ONLY by the
// gated roll in endlessDealBoonCourse (never the ordinary boon deal, never a Jackpot, never a gambit).
const EndlessTheme endlessBreakthroughThemes[] = {
	{ ENDLESS_MOD_BREAKTHROUGH,                            "Breakthrough" },
	{ ENDLESS_MOD_BREAKTHROUGH | ENDLESS_MOD_BOUNTY,       "Revelation" },
	{ ENDLESS_MOD_BREAKTHROUGH | ENDLESS_MOD_FRAGILE,      "Discovery" },
	{ ENDLESS_MOD_BREAKTHROUGH | ENDLESS_MOD_OVERCHARGE,   "Epiphany" },
	{ ENDLESS_MOD_BREAKTHROUGH | ENDLESS_MOD_FAVOR,        "Eureka" },
};

// WARP (Slipstream cranked way up -- the level hurtles past) is a rare scroll THREAT with its own
// injection in endlessGenerateCourses; naming-only here, like the omni-gravity table, so it never
// enters the hostile shuffle pool.
static const EndlessTheme endlessWarpThemes[] = {
	{ ENDLESS_MOD_WARP,       "Warp Speed" },
};

// OVERLOAD (Overclock cranked way up) is a rare, brutal hostile with its own pool, injected
// rarely (see endlessGenerateCourses) rather than shuffled into the normal rotation.
const EndlessTheme endlessOverloadThemes[] = {
	{ ENDLESS_MOD_OVERLOAD,                           "Overload" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_FORTIFIED,   "Core Breach" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_DEVASTATING, "Chain Reaction" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_ELITEPACK,   "Singularity Core" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_FRENZY,      "Detonation" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_SWIFT,       "Overvelocity" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_GRAVITY,     "Implosion" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_ENRAGE,      "Overheat" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Fusion Core" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,     "Total Meltdown" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY, "Core Meltdown" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT, "Fission" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_ENRAGE, "Critical Mass" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_GRAVITY, "Chain Blast" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_ELITEPACK, "Cascade" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_OVERCLOCK, "Feedback Loop" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT, "Power Spike" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING, "Blackout" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_FRENZY | ENDLESS_MOD_ENRAGE, "Flashpoint" },
	{ ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_FRENZY | ENDLESS_MOD_GRAVITY, "Ground Zero" },
};

// Super-rare, super-hard sectors, injected explicitly (not part of the shuffle pool) so they
// stay rare -- the Apex/Legion elite tiers with an extra danger, plus pure 5-danger nightmares.
const EndlessTheme endlessRareThemes[] = {
	// -- the bare elite tiers --
	{ ENDLESS_MOD_APEX,   "Apex Swarm" },
	{ ENDLESS_MOD_LEGION, "Legion" },
	// -- pairs: an elite tier plus one danger --
	{ ENDLESS_MOD_APEX | ENDLESS_MOD_FORTIFIED,                          "Apex Titans" },
	{ ENDLESS_MOD_APEX | ENDLESS_MOD_SWIFT,                              "Apex Hunters" },
	{ ENDLESS_MOD_APEX | ENDLESS_MOD_DEVASTATING,                        "Annihilation" },
	{ ENDLESS_MOD_APEX | ENDLESS_MOD_OVERLOAD,                           "Extinction" },
	{ ENDLESS_MOD_APEX | ENDLESS_MOD_FRENZY, "Alpha Strike" },
	{ ENDLESS_MOD_APEX | ENDLESS_MOD_ENRAGE, "Omega" },
	{ ENDLESS_MOD_APEX | ENDLESS_MOD_GRAVITY, "Final Hour" },
	{ ENDLESS_MOD_APEX | ENDLESS_MOD_ELITEPACK, "Last Stand" },
	{ ENDLESS_MOD_APEX | ENDLESS_MOD_OVERCLOCK, "Endgame" },
	{ ENDLESS_MOD_LEGION | ENDLESS_MOD_FORTIFIED,                        "Iron Legion" },
	{ ENDLESS_MOD_LEGION | ENDLESS_MOD_SWIFT,                            "Blitz Legion" },
	{ ENDLESS_MOD_LEGION | ENDLESS_MOD_DEVASTATING,                      "Ragnarok" },
	{ ENDLESS_MOD_LEGION | ENDLESS_MOD_OVERLOAD,                         "Judgment Day" },
	{ ENDLESS_MOD_LEGION | ENDLESS_MOD_FRENZY, "Mass Extinction" },
	{ ENDLESS_MOD_LEGION | ENDLESS_MOD_ENRAGE, "The Reaping" },
	{ ENDLESS_MOD_LEGION | ENDLESS_MOD_GRAVITY, "Harbinger" },
	// -- triples: an elite tier plus two dangers --
	{ ENDLESS_MOD_APEX | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Apex Siege" },
	{ ENDLESS_MOD_APEX | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,   "Apex Predator" },
	{ ENDLESS_MOD_APEX | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY,    "Apex Onslaught" },
	{ ENDLESS_MOD_APEX | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING, "Event Apex" },
	{ ENDLESS_MOD_LEGION | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,     "Final Legion" },
	{ ENDLESS_MOD_LEGION | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Legion Siege" },
	// -- FIVE dangers at once, no elite tier: the Cataclysm pool (endlessRareInjections draws these
	//    with APEX and LEGION forbidden, so they stay the "just everything at once" sectors) --
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE,  "Hell Unleashed" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_GRAVITY, "Void Storm" },
	{ ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING, "Total War" },
	{ ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING, "Doomsday" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_GRAVITY, "Nemesis" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_ELITEPACK, "Leviathan" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_OVERCLOCK, "Behemoth" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_ELITEPACK, "Titanfall" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_OVERCLOCK, "Wrath of God" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_OVERCLOCK, "Purgatory" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_GRAVITY, "Tartarus" },
	{ ENDLESS_MOD_TOPSY | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,   "Kaleidoscope" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,  "Oubliette" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE, "Naked Siege" },
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_TOPSY | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,  "Sensory Overload" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_GRAVITY,   "Doomtide" },
	{ ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_ELITEPACK,    "Deathstorm" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_GRAVITY, "Ruination" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_SHIELDLESS, "Death Knell" },
	{ ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE | ENDLESS_MOD_TOPSY | ENDLESS_MOD_SLUGGISH, "Black Sun" },
};

// "The End" -- the sector every GRAND (100th-zone) milestone deals. It is NOT a fixed bitset: only
// its CORE is constant, and the rest is re-rolled per milestone off the seeded stream, so a run's
// zone-100 finisher differs from its zone-200 one and from every other run's. Naming, the END rank,
// the FINALITY danger word and the bounty all hang off the ENDLESS_MOD_THEEND marker rather than on
// matching an exact combination, which is what lets the dangers vary freely.
//
// The CORE is the enemy at its worst -- every enemy-stat lever at once -- and nothing else. Kept out
// on purpose: the homing tiers, which turn a gun fight into a chase, and the two handicaps that
// simply take a system away from you (Shieldless, Deadgen). What varies is the special-enemy tier,
// the scroll pace, a coin each for the well / flipped view / slowed ship, and a coin each for the
// four reactive dangers (Martyrdom, Seeker, Static Discharge, Retaliation) -- 1280 combinations,
// each still recognisably The End.
#define ENDLESS_THEEND_CORE (ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT \
                             | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_ENRAGE)

Uint64 endlessMakeTheEndMods(void)
{
	Uint64 m = ENDLESS_MOD_THEEND | ENDLESS_THEEND_CORE;

	// One special-enemy tier, always: every enemy an elite, or -- less often -- every one a champion.
	// Elite Pack is deliberately not offered: deep runs retire it as redundant (see
	// endlessFixRedundantElitePack), which would rewrite the finale's bitset out from under it.
	m |= (endlessRand() % 3 == 0) ? ENDLESS_MOD_LEGION : ENDLESS_MOD_APEX;

	// One scroll pace, sometimes none -- the level can hold still or come at you at any speed.
	static const Uint64 scroll[] = {
		0, ENDLESS_MOD_SLIPSTREAM, ENDLESS_MOD_OVERCLOCK, ENDLESS_MOD_OVERLOAD, ENDLESS_MOD_WARP,
	};
	m |= scroll[endlessRand() % COUNTOF(scroll)];

	// A coin each for the hazards that act on the ship rather than the enemy. Gravity and Sluggish can
	// both land -- that is the Tar Pit pairing, brutal but always flyable: endlessGravityDrift scales
	// the pull down in lock-step with the ship, so full throttle still climbs.
	if (endlessRand() % 2)
		m |= ENDLESS_MOD_GRAVITY;
	if (endlessRand() % 2)
		m |= ENDLESS_MOD_TOPSY;
	if (endlessRand() % 2)
		m |= ENDLESS_MOD_SLUGGISH;

	// A coin each for the four reactive dangers, all independent -- The End can roll any mix of them
	// (or none) on top of the core, so no two finishers punish the same way. Static is safe here: the
	// core deliberately omits DEADGEN, so the Static/DEADGEN incompatibility never arises.
	if (endlessRand() % 2)
		m |= ENDLESS_MOD_MARTYRDOM;
	if (endlessRand() % 2)
		m |= ENDLESS_MOD_SEEKER;
	if (endlessRand() % 2)
		m |= ENDLESS_MOD_STATIC;
	if (endlessRand() % 2)
		m |= ENDLESS_MOD_RETALIATION;

	return m;
}

// EVIL Turbodrive / Overdrive: hostile mirrors of the two boons -- they SLOW your fire (Evil
// Overdrive also cuts damage) as your kill combo climbs. Injected as rare pickable course
// sectors; the three bare bits are ALSO forced gamble outcomes (EGO_CURSE_*), independent of
// courses. Adding a row here auto-wires its name/monitor/payout (see endlessFindTheme).
const EndlessTheme endlessEvilThemes[] = {
	{ ENDLESS_MOD_BACKFIRE,                           "Backfire" },
	{ ENDLESS_MOD_BURNOUT,                            "Burnout" },
	{ ENDLESS_MOD_MISFIRE,                            "Misfire" },
	{ ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_DEVASTATING, "Sitting Duck" },
	{ ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_FORTIFIED,   "Uphill Battle" },
	{ ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_SWIFT,       "Overwhelmed" },
	{ ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_GRAVITY,     "Dead Weight" },
	{ ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_FRENZY,      "Friendly Fire" },
	{ ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_ENRAGE,      "Slow Burn" },
	{ ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_ELITEPACK,   "Cornered" },
	{ ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_OVERCLOCK,   "Vapor Lock" },
	{ ENDLESS_MOD_BURNOUT | ENDLESS_MOD_DEVASTATING,  "Death Rattle" },
	{ ENDLESS_MOD_BURNOUT | ENDLESS_MOD_FORTIFIED,    "Losing Battle" },
	{ ENDLESS_MOD_BURNOUT | ENDLESS_MOD_ENRAGE,       "Downward Spiral" },
	{ ENDLESS_MOD_BURNOUT | ENDLESS_MOD_ELITEPACK,    "Outclassed" },
	{ ENDLESS_MOD_BURNOUT | ENDLESS_MOD_FRENZY,       "Flatline" },
	{ ENDLESS_MOD_BURNOUT | ENDLESS_MOD_SWIFT,        "Cinders" },
	{ ENDLESS_MOD_BURNOUT | ENDLESS_MOD_GRAVITY,      "Tailspin" },
	{ ENDLESS_MOD_BURNOUT | ENDLESS_MOD_OVERCLOCK,    "System Failure" },
	{ ENDLESS_MOD_MISFIRE | ENDLESS_MOD_DEVASTATING,  "Peashooter" },
	{ ENDLESS_MOD_MISFIRE | ENDLESS_MOD_FORTIFIED,    "Stonewall" },
	{ ENDLESS_MOD_MISFIRE | ENDLESS_MOD_SWIFT,        "Outgunned" },
	{ ENDLESS_MOD_MISFIRE | ENDLESS_MOD_FRENZY,       "Popgun" },
	{ ENDLESS_MOD_MISFIRE | ENDLESS_MOD_ENRAGE,       "Fizzle" },
	{ ENDLESS_MOD_MISFIRE | ENDLESS_MOD_GRAVITY,      "Downhill" },
	{ ENDLESS_MOD_MISFIRE | ENDLESS_MOD_ELITEPACK,    "No Contest" },
	{ ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Forced March" },
	{ ENDLESS_MOD_BURNOUT  | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,     "Last Legs" },
	{ ENDLESS_MOD_MISFIRE  | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT,       "Stalemate" },
	{ ENDLESS_MOD_BURNOUT  | ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING,    "No Way Out" },
};

// Reactor Redline: the gamble "Overheat" deal loose in the wild -- your kills scream the guns faster
// (Turbodrive) while the redlined core steadily cooks the hull (the OVERHEAT chip DoT). Its own tiny
// pool so endlessFindTheme can name it and endlessGenerateCourses can inject it super-rarely (like
// Kamikaze / Overload) -- fast fire welded to a self-inflicted burn.
const EndlessTheme endlessRedlineThemes[] = {
	{ ENDLESS_MOD_OVERHEAT | ENDLESS_MOD_TURBODRIVE,                      "Reactor Redline" },
	{ ENDLESS_MOD_OVERHEAT | ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_FRENZY, "Redline Frenzy" },
};

// SLUGGISH + GRAVITY: the "heavy, inescapable" nightmares -- the ship crawls WHILE dragged down.
// Survivable by design (endlessGravityDrift slows the pull in lock-step with the ship, so full
// throttle still climbs), but brutal -- its own tiny pool injected RARELY (like Kamikaze / Overload)
// rather than shuffled into the rotation. The bare pairing is the headline "Tar Pit".
const EndlessTheme endlessSluggishThemes[] = {
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_GRAVITY,                           "Tar Pit" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING, "Quicksand" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_FRENZY,      "Quagmire" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_SWIFT,       "Sinkhole" },
	{ ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_FORTIFIED,   "Abyss" },
};

// DEADGEN (dead generator): shields never refill AND the main gun is starved to a sputter -- a super-
// rare, evil sabotage sector with its own pool (injected ~1/55; never in the combinable pool or the
// shuffle). Rear guns / sidekicks / specials still work, so it's brutal, not unwinnable.
const EndlessTheme endlessDeadgenThemes[] = {
	{ ENDLESS_MOD_DEADGEN,                            "Dead Reactor" },
	{ ENDLESS_MOD_DEADGEN | ENDLESS_MOD_DEVASTATING,  "Defenseless" },
	{ ENDLESS_MOD_DEADGEN | ENDLESS_MOD_FRENZY,       "Powerless" },
	{ ENDLESS_MOD_DEADGEN | ENDLESS_MOD_SWIFT,        "Brownout" },
	{ ENDLESS_MOD_DEADGEN | ENDLESS_MOD_ELITEPACK,    "Cold Start" },
};

// MARTYRDOM: a destroyed enemy fires a final radial burst (4 normal / 6 elite / 8 champion), once per
// linked enemy, suppressed when the shot pool is nearly full. A RARE signature sector with its own pool
// (injected ~1/22 in endlessGenerateCourses, like Kamikaze / Overload), not part of the shuffle.
const EndlessTheme endlessMartyrdomThemes[] = {
	{ ENDLESS_MOD_MARTYRDOM,                            "Martyrdom" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_FORTIFIED,    "Last Rites" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_ELITEPACK,    "Dead Man's Volley" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_SWIFT,        "Parting Shot" },
	{ ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_DEVASTATING,  "Final Salvo" },
};

// SEEKER ROUNDS: each enemy projectile makes ONE limited course correction toward you ~0.5s after
// firing (a single ~23-degree turn, not continuous homing). A RARE signature sector with its own pool
// (injected ~1/24), not part of the shuffle. Its Swift pairing carries the +4 Seeker+Swift synergy.
const EndlessTheme endlessSeekerThemes[] = {
	{ ENDLESS_MOD_SEEKER,                            "Seeker Rounds" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_SWIFT,        "Guided Fire" },   // the +4 Seeker+Swift synergy sector
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_FRENZY,       "Smart Swarm" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_DEVASTATING,  "Lock On" },
	{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_FORTIFIED,    "Tracker Rounds" },
};

// NAMING ONLY: the bare omnidirectional gravity well gets its own headline so Chart-a-Course reads it
// as its own thing. Generation never draws from this table (the OMNI bit is added by the 50% roll on
// any gravity course in endlessGenerateCourses); it only supplies the name. Every OMNI *combo* has no
// entry here and falls through to its plain-gravity twin's name via the mask in endlessFindTheme.
static const EndlessTheme endlessGravityOmniThemes[] = {
	{ ENDLESS_MOD_GRAVITY | ENDLESS_MOD_GRAVITY_OMNI, "Rogue Well" },
};

// MIXED "gambit" sectors: a real BOON welded to real DANGER, so the sector reads as risk AND reward
// (threats on the monitor's red column, boons on the green). Every entry pairs a boon with hostiles
// on DIFFERENT levers, so nothing cancels: FRAGILE never rides FORTIFIED (opposite HP), DILATION never
// rides SWIFT/OVERCLOCK (opposite shot speed), and at most one kill-fire boon appears (they share one
// stack). Naming/monitor/payout are all driven by endlessModTable, so these rows are purely cosmetic
// labels -- generation grafts boons onto hostile courses (endlessPickMixBoon) and the matches land here;
// anything unlisted falls through to the "gambit" generic names. FRAGILE|DEVASTATING is intentionally
// absent -- it's the hostile table's "Glass Cannon".
static const EndlessTheme endlessMixedThemes[] = {
	// -- doubles --
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_FORTIFIED,   "Can Opener" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_FRENZY,      "Return Fire" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_SWIFT,       "Quickdraw" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_DEVASTATING, "Standoff" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_ELITEPACK,   "Giant Slayer" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_GRAVITY,     "Heavy Hitter" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_ENRAGE,      "Beat the Clock" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_SHIELDLESS,  "All In" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_TOPSY,       "Topsy Duel" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_FRENZY,         "Paper Storm" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_SWIFT,          "Glass Darts" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_ELITEPACK,      "Brittle Elites" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_ENRAGE,         "Short Fuse" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_GRAVITY,        "Feather Fall" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_OVERCLOCK,      "Blur" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_SHIELDLESS,     "Trade Blows" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_FORTIFIED,     "War of Patience" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_FRENZY,        "Slow Motion" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_DEVASTATING,   "Read the Room" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_ELITEPACK,     "Matrix" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_GRAVITY,       "Deep Focus" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_ENRAGE,        "Steady Hand" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_SHIELDLESS,    "Cold Read" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_TOPSY,         "Slow Spin" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_FORTIFIED,       "Big Game" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_FRENZY,          "Hot Zone" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_SWIFT,           "High Stakes" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_DEVASTATING,     "Danger Pay" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_ELITEPACK,       "Trophy Hunt" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_GRAVITY,         "Deep Dive" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_ENRAGE,          "Overtime" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_OVERCLOCK,       "Rush Job" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_SHIELDLESS,      "Hazard Bonus" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_FORTIFIED,   "Grind" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_FRENZY,      "Trigger Happy" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_SWIFT,       "Fast Hands" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_ELITEPACK,   "Cull the Herd" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_ENRAGE,      "Second Wind" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_GRAVITY,     "Dig In" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_FORTIFIED,        "Toll Road" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_SWIFT,            "Hazard Discount" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_DEVASTATING,      "Combat Pay" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_FRENZY,           "Loss Leader" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_FORTIFIED,    "Sledge" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_SWIFT,        "Piercer" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_ELITEPACK,    "Headhunter" },
	{ ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_FORTIFIED,    "Snowball" },
	{ ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_ELITEPACK,    "Killstreaker" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_SLIPSTREAM,     "Blitz" },
	{ ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_BOUNTY,      "Smash and Grab" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_SLIPSTREAM,    "Time Warp" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_SLIPSTREAM,  "Power Play" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_SLIPSTREAM,  "Payday" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_FORTIFIED,   "Tough Crowd" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_FRENZY,      "Crowd Control" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_SWIFT,       "Skeleton Crew" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_ELITEPACK,   "Demotion" },
	{ ENDLESS_MOD_NOELITE | ENDLESS_MOD_FORTIFIED,   "Grunt Work" },
	{ ENDLESS_MOD_NOELITE | ENDLESS_MOD_DEVASTATING, "Green Troops" },
	// The later boons grafted onto danger. Same rule as every row above: the boon and the threat sit on
	// DIFFERENT levers, so the red and green monitor columns never contradict each other.
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_DEVASTATING,       "Held Line" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_FRENZY,            "Storm Shelter" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_ELITEPACK,         "Shield Wall" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_FRENZY,       "Thread the Needle" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_SWIFT,        "Slip Through" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_FORTIFIED,    "Small Target" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_FRENZY,       "Flak Run" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_FORTIFIED,    "Screened Advance" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_ELITEPACK,   "Paper Giants" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_APEX,        "Hollow Crown" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_ELITEPACK,  "Silent Order" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_LEGION,     "Broken Chain" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_ELITEPACK,     "Chain Collapse" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_FRENZY,        "Pressure Valve" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_KAMIKAZE,    "Bounce Off" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_HOMING,      "Glancing Blows" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_ELITEPACK,   "Bumper Cars" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_STATIC,       "Backup Cells" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_DEVASTATING,  "Reserve Power" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_FORTIFIED,    "Survey Run" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_ENRAGE,       "Scout's Toll" },

	// -- triples --
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT,       "Armor Piercer" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,          "Counterstrike" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Slugfest" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_SWIFT,       "Elite Duel" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING,    "Trading Blows" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING,   "Sinker" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY,        "Bullet Dance" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING,      "Zen Garden" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING,   "Stonewall Zen" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FRENZY,        "Slow Dance" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,             "Confetti" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,        "Glass Storm" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_FRENZY | ENDLESS_MOD_ENRAGE,            "Tinderbox" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_SWIFT,          "Spun Glass" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT,           "Bounty Board" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING,        "Blood Money" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,         "Dead or Alive" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FORTIFIED,       "Kingpin" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,          "Frenzy Feed" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_ELITEPACK,   "Grinder" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT,            "Risk Premium" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT,        "Overpenetrate" },
	{ ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,           "Chain Lightning" },

	// -- quads --
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,      "Last Word" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING, "Overmatch" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING,  "Eye of the Storm" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,       "Shattering" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_ENRAGE,            "Powder Keg" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,        "Death and Taxes" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,          "Payday Run" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT,   "Elite Overmatch" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,      "Feeding Frenzy" },

	// -- rare gambits: TWO boons welded to real danger (bigger upside, bigger risk) --
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_DILATION | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Perfect Storm" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,           "Blood Bargain" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FORTIFIED,     "High Roller" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_DEVASTATING, "Bullet Ballet" },

	// -- the rest of the boon x ordinary-danger grid. A gambit welds exactly ONE boon onto a hostile
	//    course, so every pair below is a shape endlessPickMixBoon can actually produce (Overdrive's
	//    rows come from a shop purchase folded onto a hostile course instead). The same-lever holes
	//    stay holes: no Fragile+Fortified, no Dilation+Swift/Overclock, and NOELITE keeps just a
	//    handful of rows so the stronger no-elite-tier boon stays the rarer label. --
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_FORTIFIED,         "Long Siege" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_SWIFT,             "Deflection" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_ENRAGE,            "Hold Fast" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_GRAVITY,           "Anchor Point" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_OVERCLOCK,         "Weathered" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_TOPSY,             "Turtle" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_STATIC,            "Insulated" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_SHIELDLESS,        "Last Shield" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_RETALIATION,       "Braced" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_MARTYRDOM,         "Blast Door" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_SEEKER,            "Turned Aside" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_SLIPSTREAM,        "Fast Guard" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_SLUGGISH,          "Slow and Steady" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_FORTIFIED,    "Long Burn" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_FRENZY,       "Steady Feed" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_SWIFT,        "Quick Charge" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_ENRAGE,       "Endurance Run" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_GRAVITY,      "Counterweight" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_OVERCLOCK,    "Load Balance" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_ELITEPACK,    "Spare Cells" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_TOPSY,        "Gyro Power" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_SHIELDLESS,   "Idle Reactor" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_RETALIATION,  "Cool Under Fire" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_MARTYRDOM,    "Shrug Off" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_SEEKER,       "Recharged" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_SLIPSTREAM,   "Fast Charge" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_SLUGGISH,     "Trickle Charge" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_TOPSY,            "Flipped Fortune" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_STATIC,           "Power Bill" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_RETALIATION,      "Blood Wages" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_MARTYRDOM,        "Death Benefit" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_SEEKER,           "Marked Money" },
	{ ENDLESS_MOD_BOUNTY | ENDLESS_MOD_SLUGGISH,         "Slow Money" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_FORTIFIED,  "Silent Giants" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_FRENZY,     "Muffled" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_SWIFT,      "Signal Loss" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_DEVASTATING, "Dulled Edge" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_ENRAGE,     "Calm Command" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_GRAVITY,    "Quiet Descent" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_OVERCLOCK,  "Signal Jam" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_TOPSY,      "Crossed Wires" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_STATIC,     "White Noise" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_SHIELDLESS, "Radio Silence" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_RETALIATION, "No Reply" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_MARTYRDOM,  "Silent Fall" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_SEEKER,     "Lost Signal" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_SLIPSTREAM, "Clear Channel" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_SLUGGISH,   "Slow Signal" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_STATIC,         "Slow Current" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_RETALIATION,    "Even Tempo" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_MARTYRDOM,      "Drifting Debris" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_SEEKER,         "Wide Arc" },
	{ ENDLESS_MOD_DILATION | ENDLESS_MOD_SLUGGISH,       "Half Speed" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_ENRAGE,            "Rush Discount" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_GRAVITY,           "Sinking Fund" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_OVERCLOCK,         "Quick Sale" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_ELITEPACK,         "Elite Rebate" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_TOPSY,             "Upside Deal" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_STATIC,            "Surge Pricing" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_SHIELDLESS,        "Bare Bargain" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_RETALIATION,       "Grudge Rate" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_MARTYRDOM,         "Funeral Costs" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_SEEKER,            "Marked Down" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_SLIPSTREAM,        "Quick Bargain" },
	{ ENDLESS_MOD_FAVOR | ENDLESS_MOD_SLUGGISH,          "Slow Trade" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_SWIFT,        "Thinned Volley" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_DEVASTATING,  "Fewer and Harder" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_ENRAGE,       "Held Tide" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_GRAVITY,      "Sheltered Dive" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_OVERCLOCK,    "Filtered Storm" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_ELITEPACK,    "Elite Screen" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_TOPSY,        "Upside Screen" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_STATIC,       "Shielded Grid" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_SHIELDLESS,   "Thin Cover" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_RETALIATION,  "Curbed" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_MARTYRDOM,    "Blunted Burst" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_SEEKER,       "Fewer Hunters" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_SLIPSTREAM,   "Screened Run" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_SLUGGISH,     "Slow Screen" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_TOPSY,           "Glass Ceiling" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_STATIC,          "Brittle Circuit" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_RETALIATION,     "Fragile Fury" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_MARTYRDOM,       "Glass Grenade" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_SEEKER,          "Brittle Hunters" },
	{ ENDLESS_MOD_FRAGILE | ENDLESS_MOD_SLUGGISH,        "Crawl and Crush" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_FORTIFIED,   "Hollow Hulls" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_FRENZY,      "Hollow Volley" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_SWIFT,       "Toppling" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_DEVASTATING, "Heavy Hands" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_ENRAGE,      "Fading Giants" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_GRAVITY,     "Falling Giants" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_OVERCLOCK,   "Rushed Giants" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_TOPSY,       "Fallen Sky" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_STATIC,      "Drained Giants" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_SHIELDLESS,  "Bare Hunt" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_RETALIATION, "Angry Giants" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_MARTYRDOM,   "Dying Giants" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_SEEKER,      "Hunted Hunters" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_SLIPSTREAM,  "Passing Giants" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_SLUGGISH,    "Slow Giant Hunt" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_DEVASTATING,  "Hard to Hit" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_ENRAGE,       "Slim Odds" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_GRAVITY,      "Narrow Descent" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_OVERCLOCK,    "Slipping Through" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_ELITEPACK,    "Under Notice" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_TOPSY,        "Tucked In" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_STATIC,       "Grazed" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_SHIELDLESS,   "Thin Margin" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_RETALIATION,  "Hard Target" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_MARTYRDOM,    "Between Bursts" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_SEEKER,       "Slippery" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_SLIPSTREAM,   "Narrow Lane" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_SLUGGISH,     "Small Steps" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_DEVASTATING,     "No Overlords" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_ENRAGE,          "Unled Fury" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_GRAVITY,         "Sinking Ranks" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_OVERCLOCK,       "Rushed Ranks" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_TOPSY,           "Upended Order" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_STATIC,          "Shocked Ranks" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_SHIELDLESS,      "Fair Fight" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_RETALIATION,     "Leaderless Rage" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_MARTYRDOM,       "Loyal to the End" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_SEEKER,          "Grunt Guidance" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_SLIPSTREAM,      "Quick Ranks" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_SLUGGISH,        "Slow Ranks" },
	{ ENDLESS_MOD_NOELITE | ENDLESS_MOD_FRENZY,          "Conscripts" },
	{ ENDLESS_MOD_NOELITE | ENDLESS_MOD_SWIFT,           "Raw Recruits" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_FRENZY,        "Rising Blast" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_DEVASTATING,   "Slugger" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_ENRAGE,        "Race the Rage" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_GRAVITY,       "Heavy Blast" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_OVERCLOCK,     "Blast Tempo" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_TOPSY,         "Inverted Blast" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_STATIC,        "Blast and Bleed" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_SHIELDLESS,    "Big Swing" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_RETALIATION,   "Trade Fire" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_MARTYRDOM,     "Blast Wake" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_SEEKER,        "Blast Tracker" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_SLIPSTREAM,    "Blast Past" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_SLUGGISH,      "Slow Sledge" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_OVERCLOCK,    "Duel of Speed" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_STATIC,       "Power Trade" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_RETALIATION,  "Answer in Kind" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_MARTYRDOM,    "Kill and Cover" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_SEEKER,       "Shoot the Curve" },
	{ ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_SLUGGISH,     "Heavy Punch" },
	{ ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_FRENZY,        "Feeding Time" },
	{ ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_SWIFT,         "Momentum Shift" },
	{ ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_DEVASTATING,   "Heavyweight" },
	{ ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_ENRAGE,        "Arms Race" },
	{ ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_GRAVITY,       "Gathering Speed" },
	{ ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_STATIC,        "Power Struggle" },
	{ ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_SHIELDLESS,    "Bare Knuckles" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_FORTIFIED,     "Hard Reset" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_SWIFT,         "Clean Air" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_DEVASTATING,   "Blast Radius" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_ENRAGE,        "Reset the Clock" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_GRAVITY,       "Pressure Wave" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_OVERCLOCK,     "Cadence Break" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_TOPSY,         "Spin Clear" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_STATIC,        "Ion Sweep" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_SHIELDLESS,    "Breathing Space" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_RETALIATION,   "Cut the Chain" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_MARTYRDOM,     "Blast for Blast" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_SEEKER,        "Wipe the Curve" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_SLIPSTREAM,    "Clearing Run" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_SLUGGISH,      "Slow Sweep" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_FORTIFIED,   "Shoulder Check" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_FRENZY,      "Push Through" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_SWIFT,       "Brush Past" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_DEVASTATING, "Hard Knocks" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_ENRAGE,      "Ride It Out" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_GRAVITY,     "Soft Descent" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_OVERCLOCK,   "Rough Ride" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_TOPSY,       "Roll With It" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_STATIC,      "Fender Bender" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_SHIELDLESS,  "Bare Bumper" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_RETALIATION, "Shoved Aside" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_MARTYRDOM,   "Blast Cushion" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_SEEKER,      "Nudged" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_SLIPSTREAM,  "Sideswipe" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_SLUGGISH,    "Slow Nudge" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_FRENZY,       "Fire Survey" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_SWIFT,        "Quick Chart" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_DEVASTATING,  "Costly Chart" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_GRAVITY,      "Deep Chart" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_OVERCLOCK,    "Rushed Survey" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_ELITEPACK,    "Guarded Survey" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_TOPSY,        "Skewed Chart" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_STATIC,       "Static Survey" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_SHIELDLESS,   "Bare Recon" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_RETALIATION,  "Hot Survey" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_MARTYRDOM,    "Blast Survey" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_SEEKER,       "Tracked Recon" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_SLIPSTREAM,   "Flyby Survey" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_SLUGGISH,     "Slow Recon" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_DEVASTATING,  "Beat the Blow" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_OVERCLOCK,    "Tempo War" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_TOPSY,        "Spin and Spray" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_STATIC,       "Spark Feed" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_SHIELDLESS,   "No Margin" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_RETALIATION,  "Tempo Duel" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_MARTYRDOM,    "Rolling Thunder" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_SEEKER,       "Outpace" },
	{ ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_SLUGGISH,     "Rooted Rush" },

	// -- gambit triples for the later boons: a hostile course is often two or three bits before the
	//    boon is grafted on, so these shapes are as common as the pairs above --
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY,          "Dug In" },
	{ ENDLESS_MOD_AEGIS | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,         "Storm Break" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,         "Eye of the Needle" },
	{ ENDLESS_MOD_LOWPROFILE | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Small Mercies" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,         "Steady Burn" },
	{ ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_ENRAGE,     "War of Reserves" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY,    "Bump and Run" },
	{ ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_SWIFT,     "Traffic" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_DEVASTATING, "Domino" },
	{ ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,          "Clean Break" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_DEVASTATING, "Clay Feet" },
	{ ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY,    "Hollow Wall" },
	{ ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_SWIFT,    "Quiet Ranks" },
	{ ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,         "Thinned Storm" },
	{ ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Hard Won Map" },
	{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,            "Rank and Fire" },
	{ ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_FRENZY | ENDLESS_MOD_DEVASTATING,    "Sledge Storm" },
	{ ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING, "Rolling Start" },
};

// Every named-theme pool, in LOOKUP ORDER: the first pool holding an exact bitset names it, so
// the more specific pools come first. Adding a pool is one row here -- nothing else to update.
#define THEME_POOL(t) { (t), COUNTOF(t) }
static const struct { const EndlessTheme *tbl; unsigned n; } endlessThemePools[] = {
	THEME_POOL(endlessGravityOmniThemes),
	THEME_POOL(endlessHostileThemes),
	THEME_POOL(endlessMixedThemes),
	THEME_POOL(endlessKamikazeThemes),
	THEME_POOL(endlessHomingThemes),
	THEME_POOL(endlessOverloadThemes),
	THEME_POOL(endlessBoonThemes),
	THEME_POOL(endlessRareThemes),
	THEME_POOL(endlessEvilThemes),
	THEME_POOL(endlessRedlineThemes),
	THEME_POOL(endlessSluggishThemes),
	THEME_POOL(endlessDeadgenThemes),
	THEME_POOL(endlessMartyrdomThemes),
	THEME_POOL(endlessSeekerThemes),
	THEME_POOL(endlessBreakthroughThemes),
	THEME_POOL(endlessWarpThemes),
};
#undef THEME_POOL

// Find the theme for an exact effect-bit set (NULL = Calm / no modifiers).
static const EndlessTheme *endlessFindTheme(Uint64 mods)
{
	for (unsigned p = 0; p < COUNTOF(endlessThemePools); ++p)
		for (unsigned i = 0; i < endlessThemePools[p].n; ++i)
			if (endlessThemePools[p].tbl[i].mods == mods)
				return &endlessThemePools[p].tbl[i];
	// OMNI fallthrough: an omnidirectional gravity combo with no exact name reads as its plain-gravity
	// twin (Dense Matter, Event Horizon, ...). Strip the cosmetic OMNI bit and retry once -- the retry
	// has no OMNI bit, so it can't recurse further.
	if (mods & ENDLESS_MOD_GRAVITY_OMNI)
		return endlessFindTheme(mods & ~ENDLESS_MOD_GRAVITY_OMNI);
	return NULL;
}

// Pick a random theme's mods from a table, restricted to entries that include ALL `must` bits
// and NONE of the `forbid` bits. This lets the injections draw straight from the name tables
// (endlessBoonThemes / endlessRareThemes) -- so adding a row there makes that sector appear,
// with no separate injection pool to keep in sync. Returns `must` if nothing matches.
Uint64 endlessPickThemeMods(const EndlessTheme *tbl, unsigned count, Uint64 must, Uint64 forbid)
{
	unsigned n = 0;
	for (unsigned i = 0; i < count; ++i)
		if ((tbl[i].mods & must) == must && (tbl[i].mods & forbid) == 0)
			++n;
	if (n == 0)
		return must;
	unsigned pick = endlessRand() % n;
	for (unsigned i = 0; i < count; ++i)
		if ((tbl[i].mods & must) == must && (tbl[i].mods & forbid) == 0)
			if (pick-- == 0)
				return tbl[i].mods;
	return must;  // unreachable
}

// Evocative names for un-curated (randomly generated) combos, picked deterministically per bitset so a
// given combo always reads the same. Three flavors so an un-named combo still reads the RIGHT tone: an
// ominous word for pure danger, a fortunate word for a pure boon combo, a "gambit" word for a mixed one.
// Big pools keep same-chart hash collisions rare (the unique-name pass in endlessGenerateCourses catches
// the rest). Every word must be unique across ALL name tables (curated included -- see the dup scan in
// notes.md) and stick to font_ascii-displayable characters: letters, space, apostrophe, hyphen.
static const char *const endlessGenericNames[] = {
	"Havoc", "Chaos", "Carnage", "Ruin", "Fury", "Terror", "Doom", "Peril",
	"Menace", "Scourge", "Bedlam", "Mayhem", "Torment", "Dread", "Malice",
	"Ordeal", "Gauntlet", "Crucible", "Inferno", "Tempest", "Reckoning",
	"Oblivion", "Rampage", "Turmoil", "Onset", "Affliction",
	"Anguish", "Vengeance", "Spite", "Blight", "Bane", "Calamity",
	"Catastrophe", "Shockwave", "Aftershock", "Fallout", "Deluge", "Torrent",
	"Broadside", "Dragnet", "Crosshairs", "Deathtrap", "Minefield", "Gallows",
	"Ill Omen", "Tribulation", "Strife", "Discord", "Incursion", "Squall",
	"Whirlwind", "Ashfall", "Backdraft", "Misfortune", "Jeopardy", "Hazard",
	"Distress", "Duress", "Snare", "Pitfall", "Vendetta", "Hostile Ground",
	"Scorched Sky", "Grim Tide", "Nightfall", "Darkfall", "Downpour",
	"Hornet Nest", "Viper Pit", "Killing Floor", "Furnace", "Cauldron",
	"Ravage", "Rupture", "Sundering", "Retribution", "Reprisal",
	"Dire Straits", "Storm Front", "Headwinds", "Foul Weather", "Wasteland",
	"Badlands", "Rough Waters", "Breaking Point", "Boiling Point",
	"Fever Pitch", "Graveyard", "Onrush",
	"Abattoir", "Adversity", "Agony", "Anathema", "Ashen Sky", "Avalanche",
	"Bad Moon", "Black Ice", "Bleak Passage", "Blood Debt", "Bloodletting",
	"Brutality", "Burning Sky", "Choke Point", "Cold Comfort", "Corrosion",
	"Crossbones", "Cutthroat", "Dark Water", "Dead Air", "Dead End",
	"Death Wish", "Desperation", "Dirge", "Downfall", "Dust Storm",
	"Evil Hour", "Famine", "Fell Wind", "Firetrap", "Foul Tide", "Grief",
	"Grim Harvest", "Hail of Iron", "Hard Luck", "Hard Road", "Harrowing",
	"Hellscape", "Ice Storm", "Ill Wind", "Iron Teeth", "Jaws of Death",
	"Kill Order", "Last Breath", "Locust Swarm", "Long Night", "Lost Cause",
	"Malignant", "Mauling", "Meat Grinder", "Misery", "Molten Rain",
	"No Man's Land", "Obliteration", "Open Season", "Plague", "Poison Well",
	"Quicklime", "Rain of Fire", "Rat Trap", "Razorwind", "Red Sky",
	"Ruinous Path", "Savagery", "Scorn", "Shattered Sky", "Sinister",
	"Slaughterhouse", "Sorrow", "Stormlash", "Strangler", "Suffering",
	"Thorn Field", "Thunderhead", "Trial by Fire", "Undoing", "Unrest",
	"Venom", "Vise Grip", "War Drums", "Woe", "Wreckage",
};
static const char *const endlessBoonGenericNames[] = {
	"Godsend", "Reprieve", "Tailwind", "Grace", "Easy Street", "Good Omen",
	"Lucky Star", "Silver Lining", "Uplift", "Bright Side", "Fair Winds",
	"Respite", "Blessing", "Sanctuary", "Boon", "Windswept",
	"Serendipity", "Providence", "Good Fortune", "Charmed", "Lucky Streak",
	"Golden Hour", "Halcyon", "Oasis", "Haven", "Safe Harbor", "Safe Passage",
	"Smooth Sailing", "Clear Skies", "Blue Skies", "Daybreak", "First Light",
	"New Dawn", "Morning Star", "North Star", "Guiding Light", "Beacon",
	"Lodestar", "Godspeed", "Cakewalk", "Breather", "Mercy", "Benediction",
	"Deliverance", "Prosperity", "Abundance", "Milk Run", "Sunday Drive",
	"Scenic Route", "Fair Weather", "Green Light", "All Clear", "Home Free",
	"Open Road", "Free Ride", "Sweet Spot", "Kind Stars", "Full Sails",
	"Warm Welcome", "Gentle Current", "Good Graces", "Guardian Angel",
	"Fresh Start", "Head Start", "Helping Hand", "Stroke of Luck",
	"Free Pass", "Sunrise",
	"Auspicious", "Balm", "Bright Passage", "Calm Waters", "Carefree",
	"Clean Run", "Clear Path", "Clear Sailing", "Comfort", "Cool Breeze",
	"Easy Going", "Easy Odds", "Even Keel", "Fair Shake", "Fortunate Turn",
	"Free Rein", "Friendly Skies", "Gentle Hand", "Glad Tidings",
	"Golden Path", "Golden Ticket", "Good Tidings", "Happy Accident",
	"Harmony", "High Spirits", "Idyll", "In the Clear", "Just Rewards",
	"Kind Fortune", "Lady Luck", "Leg Up", "Light Work", "Lucky Charm",
	"Lull", "Merciful", "Mild Passage", "Nest Egg", "Nice and Easy",
	"On a Roll", "Open Water", "Paradise", "Pathfinder", "Peace of Mind",
	"Plain Sailing", "Promised Land", "Quiet Sector", "Rainbow",
	"Right of Way", "Sanctum", "Second Chance", "Serenity", "Shelter",
	"Shooting Star", "Silk Road", "Slack Water", "Smooth Ride", "Solace",
	"Steady Wind", "Still Waters", "Sunbeam", "Sunlit Path", "Tranquil",
	"Upswing", "Warm Front", "Welcome Sight", "Well Wishes", "Zephyr",
};
static const char *const endlessMixedGenericNames[] = {
	"Gambit", "Trade-off", "Bargain", "Double Edge", "Wager", "Faustian",
	"Devil's Deal", "Two-Edged", "Give and Take", "Long Shot", "Roll the Dice",
	"Loaded Dice", "Bittersweet", "Mixed Bag", "Toss-Up", "Wild Card", "Crossroads",
	"Coin Flip", "Heads or Tails", "Double Down", "Ante Up", "Calculated Risk",
	"Risky Business", "Gray Area", "Fine Print", "Hidden Cost", "Price to Pay",
	"Steep Price", "Fair Trade", "Horse Trade", "Quid Pro Quo", "Tit for Tat",
	"Tug of War", "Balancing Act", "Tightrope", "Razor's Edge", "Knife's Edge",
	"Double Bind", "Dilemma", "Conundrum", "Paradox", "Pandora's Box",
	"Poison Apple", "Forbidden Fruit", "Siren Song", "Fool's Gold",
	"Fool's Errand", "Devil's Due", "Snake Eyes", "Hedged Bet", "Side Bet",
	"Long Odds", "Even Odds", "Leap of Faith", "Blind Bargain", "Gilded Cage",
	"Mixed Blessing", "Rose and Thorn", "Gift Horse", "Trojan Horse",
	"Silver Hook", "Honey Trap", "Hard Bargain", "Push Your Luck",
	"Buyer Beware", "Sucker Bet", "Raised Stakes",
	"All or Nothing", "Bait and Switch", "Best of Both", "Big If",
	"Bitter Pill", "Blood Price", "Brass Ring", "Cash and Carry", "Caveat",
	"Cold Trade", "Costly Gift", "Cut Both Ways", "Dangerous Gift",
	"Double or Nothing", "Even Trade", "Fair Exchange", "Fine Balance",
	"Give a Little", "Gilded Trap", "Grim Bargain", "Half Measure",
	"Hard Choice", "Hedge", "High Wire", "House Rules", "In for a Penny",
	"Iron Price", "Lure", "Middle Ground", "Mixed Signals", "No Free Lunch",
	"Odd Bargain", "On the Fence", "Penny Wise", "Pick Your Poison",
	"Poisoned Well", "Price of Glory", "Pros and Cons", "Rigged Game",
	"Sharp Deal", "Six of One", "Split Decision", "Stacked Deck",
	"Sweet and Sour", "Sweetened Deal", "Take the Bait", "Thin Ice",
	"Trade Winds", "Two Way Street", "Uneasy Truce", "Velvet Trap",
	"Weigh the Odds", "With Strings",
};

// `salt` steps a GENERATED pick to the next word in its list -- 0 everywhere except the per-visit
// unique-name pass in endlessGenerateCourses (two distinct bitsets can hash to the same word, and
// one chart must never offer two sectors reading the same). Curated names ignore it: the theme
// tables hold no duplicate names, so distinct combos can't clash through them.
const char *endlessComboNameSalted(Uint64 mods, unsigned salt)
{
	if (mods & ENDLESS_MOD_THEEND)
		return "The End";                  // the finale is named by its marker, whatever dangers it rolled
	const EndlessTheme *t = endlessFindTheme(mods);
	if (t)
		return t->name;                    // curated combos keep their cool names
	if (mods == 0)
		return "Calm Sector";
	// Mask the cosmetic OMNI bit so an un-named omni gravity combo shares its plain-gravity twin's
	// generated name (the direction is a runtime surprise, not a different sector on the chart).
	Uint64 key = mods & ~ENDLESS_MOD_GRAVITY_OMNI;
	Uint64 h = (key ^ (key >> 4) ^ (key >> 9)) + salt;  // mix so nearby bitsets differ
	// Classify off the danger/boon masks so the generated name matches the sector's tone. Cursed counts
	// hostile-side (it's a trap), matching how the monitor lists it.
	const bool hasHostile = (key & (ENDLESS_HOSTILE_MASK | ENDLESS_MOD_CURSED)) != 0;
	const bool hasBoon    = (key & ENDLESS_BOON_MASK) != 0;
	if (hasBoon && hasHostile)
		return endlessMixedGenericNames[h % COUNTOF(endlessMixedGenericNames)];
	if (hasBoon)
		return endlessBoonGenericNames[h % COUNTOF(endlessBoonGenericNames)];
	return endlessGenericNames[h % COUNTOF(endlessGenericNames)];
}

// --- Course danger tier ---------------------------------------------------------------------
// A sector's net danger, and the two ways it is shown: the tier WORD on the Chart-a-Course help
// line ("Danger: Severe") and the letter GRADE on the planet monitor. Both come off one score, so
// they can never disagree, and the score shares its reward table with the payout -- so a course
// that reads more dangerous always pays more.
//
// ENDLESS_HOSTILE_MASK / ENDLESS_BOON_MASK are defined earlier (with the generic name pools, which
// classify combos by tone from them); the danger score / tier / rank below reuse the same masks.

// A survival boon riding a hostile sector makes it play less deadly than its raw threat list:
// frail or crawling-shot foes, harder-hitting or kill-fed guns, or a blitz-past pass all buy time.
// endlessDangerScore credits these against the hostile total so the tier reads net danger. Credits
// are in the same reward-tenths as endlessModTable. Pure-cash boons (Bounty, Favor, Cursed) buy no
// safety, so they grant no credit and don't appear here.
static const struct { Uint64 bit; int credit; } endlessBoonMitigation[] = {
	{ ENDLESS_MOD_DILATION,    8 },  // enemy shots crawl: the biggest dodge cushion
	{ ENDLESS_MOD_FRAGILE,     8 },  // frail foes die fast: fewer guns left firing
	{ ENDLESS_MOD_NOELITE,     8 },  // no elite/champion tier at all: the tanky, hard-hitting shooters simply never appear
	{ ENDLESS_MOD_NOCHAMP,     5 },  // no champions: drops the nastiest tier (1.7x fire, +50% shot dmg, fat HP)
	{ ENDLESS_MOD_OVERCHARGE,  5 },  // shots hit harder: quicker kills
	{ ENDLESS_MOD_OVERDRIVE,   5 },  // each kill stacks fire and damage
	{ ENDLESS_MOD_OVERBLAST,   4 },  // each kill stacks damage
	{ ENDLESS_MOD_TURBODRIVE, 3 },  // each kill quickens the guns
	// The later survival boons. Star Charts and Breakthrough are absent on purpose: their reward lands
	// at the NEXT outpost, so they buy no safety inside the sector and must not soften its tier.
	{ ENDLESS_MOD_LOWPROFILE,  7 },  // a quarter off the hitbox: the broadest dodge cushion of the set
	{ ENDLESS_MOD_AEGIS,       5 },  // the shield can no longer be punched through in one hit
	{ ENDLESS_MOD_FLAKSCREEN,  5 },  // half the tide's added bullets simply never fire
	{ ENDLESS_MOD_GIANTKILLER, 5 },  // elites/champions die at ordinary speed, so their guns leave the fight sooner
	{ ENDLESS_MOD_CLEANSIGNALS,4 },  // the special tier stops firing fast and hitting hard
	{ ENDLESS_MOD_SOFTLANDING, 3 },  // ramming stops being a death sentence (projectiles still are)
	{ ENDLESS_MOD_SHOCKWAVE,   3 },  // each special kill buys a moment of clear air
	{ ENDLESS_MOD_AUXREACTOR,  3 },  // shields refill without starving the guns
};

// COMBO SYNERGIES: pairs whose danger is worse than the sum of their parts -- one bit makes the other
// harder to survive, so the pairing earns a bonus on top of the two rewards. Folded into BOTH the
// danger score (endlessDangerScoreEx) and the clear payout (endlessClearBonusForEx), so a synergy
// course both READS and PAYS like the nastier sector it is. Every entry whose bits are ALL present
// fires, and entries stack. Bits are all hostile, so a pure-boon course never triggers one; bonus is
// in the same reward-tenths as endlessModTable.
static const struct { Uint64 combo; int bonus; } endlessSynergies[] = {
	{ ENDLESS_MOD_SLUGGISH   | ENDLESS_MOD_GRAVITY,     8 },  // Tar Pit: a crawling ship dragged down -- the classic inescapable pairing
	{ ENDLESS_MOD_SLUGGISH   | ENDLESS_MOD_KAMIKAZE,    7 },  // slowed WHILE the rammers home in: you can't outrun them
	{ ENDLESS_MOD_DEADGEN    | ENDLESS_MOD_FORTIFIED,   6 },  // starved guns against tanky hulls -- kills slow to a crawl
	{ ENDLESS_MOD_SLUGGISH   | ENDLESS_MOD_HOMING,      5 },  // slowed vs light homing: dodging no longer shakes it
	{ ENDLESS_MOD_FORTIFIED  | ENDLESS_MOD_ENRAGE,      5 },  // tanky fights drag on while the fire rate climbs -- the long fight gets deadlier
	{ ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_DEVASTATING, 5 },  // no regen and every hit lands harder: one mistake sticks
	{ ENDLESS_MOD_SLUGGISH   | ENDLESS_MOD_FRENZY,      4 },  // half the reach to thread twice the bullets
	{ ENDLESS_MOD_SWIFT      | ENDLESS_MOD_HOMING,      4 },  // homing shots that are ALSO fast -- hard to outrun and hard to sidestep
	{ ENDLESS_MOD_TOPSY      | ENDLESS_MOD_GRAVITY,     4 },  // a flipped view AND a pull: which way is up, and away?
	{ ENDLESS_MOD_SEEKER      | ENDLESS_MOD_SWIFT,     4 },  // guided shots that are ALSO fast -- barely any time to read the mid-flight turn
	{ ENDLESS_MOD_RETALIATION | ENDLESS_MOD_ENRAGE,    5 },  // a kill-storm stacked on the time-based fire climb: it screams fastest exactly when you clear hardest
};

// Total synergy bonus for a modifier set: every combo whose bits are all present, summed (they stack).
int endlessSynergyBonus(Uint64 mods)
{
	int b = 0;
	for (unsigned i = 0; i < COUNTOF(endlessSynergies); ++i)
		if ((mods & endlessSynergies[i].combo) == endlessSynergies[i].combo)
			b += endlessSynergies[i].bonus;
	return b;
}

// The sector's net danger: its hostile modifiers' summed reward (endlessModTable, in tenths of the
// base) minus any survival-boon credit above. A course with no hostile bits scores 0, so the tier
// words it as Boon/Calm and never needs a number. A hostile course floors at 1, so even a heavily
// mitigated danger still reads at least "Low" rather than collapsing to a boon.
// `baseDanger` is the shipped level's intrinsic danger nudge (endless_levelprofile.h), folded into a
// HOSTILE course's score by the DISPLAY/SORT sites so its rank reflects the level too. A calm/boon
// course (no hostile bits) deliberately IGNORES baseDanger and stays at 0 -- so calm sectors always
// read Calm and always sort FIRST, whatever their level; that level's danger surfaces only in the
// PAYOUT (payoutMille, endless_shop.c), never by demoting the safe route below a hostile one. The
// plain endlessDangerScore is this with baseDanger 0 -- kept pure for the milestone generator, which
// scores bare hypothetical bitsets that have no level behind them.
int endlessDangerScoreEx(Uint64 mods, int baseDanger)
{
	const Uint64 h = mods & ENDLESS_HOSTILE_MASK;
	if (h == 0)
		return 0;   // clean/boon course: always Calm and always first, regardless of the level's baseDanger
	int t = 0;
	for (unsigned i = 0; i < COUNTOF(endlessModTable); ++i)
		if (h & endlessModTable[i].bit)
			t += endlessModTable[i].reward;
	for (unsigned i = 0; i < COUNTOF(endlessBoonMitigation); ++i)
		if (mods & endlessBoonMitigation[i].bit)
			t -= endlessBoonMitigation[i].credit;
	t += endlessSynergyBonus(mods);   // combos worse than the sum of their parts
	t += baseDanger;
	return (t < 1) ? 1 : t;   // a hostile course never reads below Low, even on a gentle level
}

int endlessDangerScore(Uint64 mods)
{
	return endlessDangerScoreEx(mods, 0);
}

// One bit's registry phrase, for screens outside the endless group (the debug zone jump labels its
// modifier rows with it). "" for a bit with no row, or one whose word is deliberately NULL -- the
// finale marker, which is a label rather than a mechanic.
const char *endlessModWord(Uint64 bit)
{
	for (unsigned i = 0; i < COUNTOF(endlessModTable); ++i)
		if (endlessModTable[i].bit == bit)
			return endlessModTable[i].word ? endlessModTable[i].word : "";
	return "";
}

// THE DANGER LADDER. One table drives both the tier WORD and the letter GRADE, so the pair can
// never disagree -- they used to be two hand-maintained if-chains that had to be kept in step.
// `maxScore` is the inclusive top of each band; the last row catches everything above it (its
// score is never read). These thresholds are the tuning knobs for how a net danger score reads
// on the Chart-a-Course monitor: they are spread so single-danger sectors fan out into distinct
// rungs rather than all landing on "Low".
static const struct { int maxScore; const char *tier; } endlessDangerBands[] = {
	{  9, "Low"        },  // grade E -- every hostile course floors at score 1, so nothing hostile reads F
	{ 13, "Moderate"   },  // grade D
	{ 19, "Tough"      },  // grade C
	{ 26, "High"       },  // grade B
	{ 33, "Severe"     },  // grade A
	{ 39, "Deadly"     },  // grade S
	{ 49, "Extreme"    },  // grade S+
	{ 59, "NIGHTMARE"  },  // grade S++
	{  0, "APOCALYPSE" },  // grade S+++ -- the catch-all; maxScore unused
};

// Which rung of the ladder a hostile score lands on: 0 (mildest) .. COUNTOF-1 (the catch-all).
static unsigned endlessDangerBand(int score)
{
	unsigned b = 0;
	while (b + 1 < COUNTOF(endlessDangerBands) && score > endlessDangerBands[b].maxScore)
		++b;
	return b;
}

// Tier word shown before a course's description: a one-glance risk read off the net danger score.
// No hostile bits is a Boon (Calm with no mods at all). A Cursed sector has no COMBAT danger, so it
// reads Boon too -- its economic catch (big cash now, empty shop next) is carried by its own red
// "cash now, empty shop" modifier row, not a separate tier/help label.
const char *endlessDangerTierEx(Uint64 mods, int baseDanger)
{
	if (mods & ENDLESS_MOD_THEEND) return "FINALITY";  // the 100th-zone finale, a rung above APOCALYPSE
	const int score = endlessDangerScoreEx(mods, baseDanger);
	if (score == 0) return (mods == 0) ? "Calm" : "Boon";  // no danger at all: clean/boon AND a gentle level
	return endlessDangerBands[endlessDangerBand(score)].tier;
}

const char *endlessDangerTier(Uint64 mods)
{
	return endlessDangerTierEx(mods, 0);
}

// Letter-grade twin of endlessDangerTier, off the same ladder: level 0 (F, no hostile bits at all)
// up to 9 (S+++), plus one off-ladder slot -- 10 (END, the finale). The numeric level is the single
// source of truth for both the letter string and the green-to-red tint the monitor draws it in
// (game_menu.c endlessRankHue[]), so those two can never drift either.
int endlessDangerRankLevelEx(Uint64 mods, int baseDanger)
{
	if (mods & ENDLESS_MOD_THEEND) return 10;   // END -- the 100th-zone finale's own grade
	const int score = endlessDangerScoreEx(mods, baseDanger);
	if (score == 0) return 0;                   // F -- genuinely no danger (a Cursed sector too: no COMBAT danger)
	return 1 + (int)endlessDangerBand(score);   // E .. S+++
}

int endlessDangerRankLevel(Uint64 mods)
{
	return endlessDangerRankLevelEx(mods, 0);
}
// Grade 10 ("END") is off the letter scale on purpose -- it belongs to the finale alone. game_menu.c's
// endlessRankHue[] is indexed by the same level, so the two arrays must stay the same length.
static const char *const endlessRankName[11] = { "F", "E", "D", "C", "B", "A", "S", "S+", "S++", "S+++", "END" };
const char *endlessDangerRankEx(Uint64 mods, int baseDanger)
{
	return endlessRankName[endlessDangerRankLevelEx(mods, baseDanger)];
}

const char *endlessDangerRank(Uint64 mods)
{
	return endlessDangerRankEx(mods, 0);
}

// --- Per-level intrinsic danger (endless_levelprofile.h) -------------------------------------
// The generated table is keyed by (episode, lvlFileNum). Endless folds baseDanger into a course's
// danger score at the DISPLAY / SORT / PAYOUT sites (endless_course.c) so the shown rank reflects
// the level too; endlessDangerScore itself stays pure (it also scores bare hypothetical bitsets
// that have no level). A level missing from the table -- which shouldn't happen for shipped data
// -- reads neutral so an unknown level never mislabels or misprices a course.
int endlessLevelBaseDanger(int ep, int file, int difficulty)
{
	if (difficulty < 0)
		difficulty = 0;
	else if (difficulty > 10)
		difficulty = 10;
	for (unsigned i = 0; i < COUNTOF(endlessLevelProfiles); ++i)
		if (endlessLevelProfiles[i].ep == ep && endlessLevelProfiles[i].file == file)
			return endlessLevelProfiles[i].baseDanger[difficulty];
	return 0;
}

// The level's fine PAYOUT term (thousandths of the base clear reward) -- decoupled from baseDanger so
// same-grade levels still pay different amounts. Folded into endlessClearBonusForEx, not the danger
// score; a missing level reads 0 (pays exactly the modifier-driven amount, no level bonus).
int endlessLevelPayoutMille(int ep, int file, int difficulty)
{
	if (difficulty < 0)
		difficulty = 0;
	else if (difficulty > 10)
		difficulty = 10;
	for (unsigned i = 0; i < COUNTOF(endlessLevelProfiles); ++i)
		if (endlessLevelProfiles[i].ep == ep && endlessLevelProfiles[i].file == file)
			return endlessLevelProfiles[i].payoutMille[difficulty];
	return 0;
}

int endlessLevelLengthClass(int ep, int file)
{
	for (unsigned i = 0; i < COUNTOF(endlessLevelProfiles); ++i)
		if (endlessLevelProfiles[i].ep == ep && endlessLevelProfiles[i].file == file)
			return endlessLevelProfiles[i].lengthClass;
	return 1;
}
