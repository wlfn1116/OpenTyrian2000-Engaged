/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "opentyr.h"
#include "config_file.h"

#include "SDL.h"

#include <stdio.h>

enum
{
	DIFFICULTY_WIMP = 0,
	DIFFICULTY_EASY,
	DIFFICULTY_NORMAL,
	DIFFICULTY_HARD,
	DIFFICULTY_IMPOSSIBLE,
	DIFFICULTY_INSANITY,
	DIFFICULTY_SUICIDE,
	DIFFICULTY_MANIACAL,
	DIFFICULTY_LORD_OF_GAME,  // "Lord of Game" on the Difficulty Level select screen
	DIFFICULTY_NORTANEOUS,
	DIFFICULTY_10,
};

// NOTE: Do not reorder.  This ordering corresponds to the keyboard
//       configuration menu and to the bits stored in demo files.
enum
{
	KEY_SETTING_UP,
	KEY_SETTING_DOWN,
	KEY_SETTING_LEFT,
	KEY_SETTING_RIGHT,
	KEY_SETTING_FIRE,
	KEY_SETTING_CHANGE_FIRE,
	KEY_SETTING_LEFT_SIDEKICK,
	KEY_SETTING_RIGHT_SIDEKICK,
};

typedef SDL_Scancode KeySettings[8];

typedef JE_byte MouseSettings[3];

typedef JE_byte JE_PItemsType[12]; /* [1..12] */

/* Credit rule attached to a recorded co-op Campaign run. */
enum
{
	COOP_CREDIT_UNKNOWN = 0,
	COOP_CREDIT_SHARED,
	COOP_CREDIT_INDIVIDUAL,
	COOP_CREDIT_INDIVIDUAL_DOUBLED,
	COOP_CREDIT_COUNT,
};

/* One co-op Campaign record per episode, separate from Arcade scores. */
typedef struct
{
	Sint64 score;      // the two players' combined cash
	char   name[30];   // both player names, as the lobby knew them
	Uint8  difficulty;
	Uint8  credit;     // one of COOP_CREDIT_*, the scale the score was earned on
}
CoopCampaignScore;

#define COOP_CAMPAIGN_SCORE_EPISODES 5   // asserted against EPISODE_MAX in config.c
extern CoopCampaignScore coopCampaignScores[COOP_CAMPAIGN_SCORE_EPISODES];
// The lobby's own word for a record's credit rule, or NULL for COOP_CREDIT_UNKNOWN.
const char *coopCampaignCreditName(Uint8 credit);
// Record the finished run if it beats that episode's standing best.
void coopCampaignScoreNote(void);

extern const KeySettings defaultKeySettings;
extern const MouseSettings defaultMouseSettings;
extern char defaultHighScoreNames[39][23];
extern char defaultTeamNames[10][25];
extern JE_boolean smoothies[9];
extern JE_byte starShowVGASpecialCode;
extern JE_word lastCubeMax, cubeMax;
extern JE_word cubeList[4];
extern JE_boolean gameHasRepeated;
extern JE_shortint difficultyLevel, oldDifficultyLevel, initialDifficulty;
extern JE_byte timeBattleSelection;
extern uint power, lastPower, powerAdd;
extern JE_byte shieldWait, shieldT;

enum
{
	SHOT_FRONT,
	SHOT_REAR,
	SHOT_LEFT_SIDEKICK,
	SHOT_RIGHT_SIDEKICK,
	SHOT_MISC,
	SHOT_P2_CHARGE,
	SHOT_P1_SUPERBOMB,
	SHOT_P2_SUPERBOMB,
	SHOT_SPECIAL,
	SHOT_NORTSPARKS,
	SHOT_SPECIAL2
};

extern JE_byte shotRepeat[11], shotMultiPos[11];
extern JE_boolean portConfigChange, portConfigDone;
extern char lastLevelName[11], levelName[11];
extern JE_byte mainLevel, nextLevel, saveLevel;
extern KeySettings keySettings;
extern MouseSettings mouseSettings;
extern JE_shortint levelFilter, levelFilterNew, levelBrightness, levelBrightnessChg;
extern JE_boolean filtrationAvail, filterActive, filterFade, filterFadeStart;
extern JE_boolean gameJustLoaded;
extern JE_boolean galagaMode;
extern JE_boolean extraGame;
// Set by the ']e' section command for the whole of the level it loads: an ENGAGE mini-game
// (** ALE **, TIME WAR, SQUADRON). Cleared at the top of every JE_loadMap.
extern JE_boolean engageMode;
extern JE_boolean twoPlayerMode, twoPlayerLinked, onePlayerAction, timedBattleMode, superTyrian, trentWin;
// Online Campaign has two independent ships but keeps the normal campaign economy and scripts.
extern JE_boolean coopCampaignMode;
// Online Endless is the same two independent ships around an Endless run (see endless.c).
extern JE_boolean coopEndlessMode;

// Online co-op of either kind: two full ships, the cash economy and a networked outpost.
static inline bool coop_mode_active(void)
{
	return coopCampaignMode || coopEndlessMode;
}

// Online Arcade with Separate ships: two personal single-player-style arcades sharing the
// level. Session-scoped, armed from the host's lobby setting; never saved.
extern JE_boolean arcadeSeparateMode;

static inline bool arcade_separate_mode(void)
{
	return twoPlayerMode && !coop_mode_active() && arcadeSeparateMode;
}

/* True when both players own complete, independent ships. */
static inline bool dual_ship_mode(void)
{
	return coop_mode_active() || arcade_separate_mode();
}

static inline bool arcade_rules_active(void)
{
	return onePlayerAction || (twoPlayerMode && !coop_mode_active());
}

// The classic linked pair: the split two-player HUD, docking, and the Dragonwing.
static inline bool split_arcade_mode(void)
{
	return twoPlayerMode && !coop_mode_active() && !arcadeSeparateMode;
}
extern JE_boolean endlessMode;  // Endless roguelite mode (see endless.c)
// Session-only switch for Endless combat effects outside Endless mode.
extern JE_boolean endlessCampaignMods;
extern JE_byte superArcadeMode;

// Per-seat custom ships are exchanged only in these modes. Fixed-hull modes are excluded.
static inline bool custom_ships_multiplayer_mode(void)
{
	// SA_NONE is zero; its enum is declared later in game_state.h.
	return !timedBattleMode && !superTyrian && superArcadeMode == 0 &&
	       (coop_mode_active() || arcade_separate_mode());
}

extern JE_byte superArcadePowerUp;
extern JE_real linkGunDirec;
extern JE_byte inputDevice[2];
extern JE_byte secretHint;
extern JE_byte background3over;
extern JE_byte background2over;
extern JE_byte gammaCorrection;
extern JE_boolean superPause, explosionTransparent, youAreCheating, background2, smoothScroll, wild, superWild, starActive, topEnemyOver, skyEnemyOverAll, background2notTransparent;
extern JE_byte fastPlay;
extern JE_boolean pentiumMode;
extern JE_byte gameSpeed;
extern JE_byte processorType;
extern int fps_cap;

/* Enhancement settings. */

typedef enum
{
	BOSS_BAR_CLASSIC  = 0,  // original small double-sided bar
	BOSS_BAR_ENHANCED = 1,  // redesigned framed gauge
} BossBarStyle;

typedef enum
{
	BOSS_BAR_TOP    = 0,  // horizontal, along the top
	BOSS_BAR_BOTTOM = 1,  // horizontal, along the bottom
	BOSS_BAR_LEFT   = 2,  // vertical, left edge
	BOSS_BAR_RIGHT  = 3,  // vertical, right edge
} BossBarLayout;

typedef enum
{
	BOSS_BAR_TWO_TOGETHER = 0,  // horizontal: stacked;       vertical: side by side on the chosen side
	BOSS_BAR_TWO_SPLIT    = 1,  // horizontal: halves side by side;  vertical: one left + one right
	BOSS_BAR_TWO_STACKED  = 2,  // horizontal: stacked;       vertical: stacked (one above the other) on the chosen side
} BossBarTwoMode;

typedef enum
{
	ENEMY_BAR_HORIZONTAL = 0,  // bar runs horizontally (fills left->right)
	ENEMY_BAR_VERTICAL   = 1,  // bar runs vertically   (fills bottom->up)
} EnemyBarLayout;

typedef enum
{
	ENEMY_BAR_POS_BOTTOM = 0,  // below the enemy (the original placement)
	ENEMY_BAR_POS_TOP    = 1,  // above the enemy
	ENEMY_BAR_POS_LEFT   = 2,  // left of the enemy
	ENEMY_BAR_POS_RIGHT  = 3,  // right of the enemy
	ENEMY_BAR_POS_CENTER = 4,  // over the enemy's centre
} EnemyBarPosition;

extern int bossBarStyle;    // BossBarStyle
extern int bossBarLayout;   // BossBarLayout
extern int bossBarTwoMode;  // BossBarTwoMode
/* Which enemies flash when a level makes them damageable. */
typedef enum
{
	VULN_CUE_OFF = 0,
	VULN_CUE_BOSSES = 1,  // bodies represented by a boss bar; default
	VULN_CUE_ALL = 2,
	VULN_CUE_COUNT
} VulnerableCueMode;

extern int vulnerableCue;   // VulnerableCueMode
extern bool armorAlarm;     // low-armor WARNING siren (Setup > Sound)
extern bool linkSounds;     // 2P fuse/unfuse clink+spring (Setup > Sound)
extern bool debugMode;      // gates the debug menu and debug level select
// Widens all-layer parallax so strafing sweeps the full map width.
extern bool extraParallax;
extern bool mirroredLayers; // over-panned layer edges continue as a flipped mirror image (both parallax modes)
extern bool enemyBars;      // show a small health bar on damaged enemies
// On-screen sidekick fire buttons. This control preference stays outside gameplay presets.
extern bool touchSidekickButtons;
// Show navigation buttons on ordinary touch menus.
extern bool touchNavButtons;
// Touch-control alpha percentage. Zero also removes their hit targets.
#define TOUCH_OPACITY_MAX     100
#define TOUCH_OPACITY_DEFAULT 100
extern int touchButtonOpacity;
extern int enemyBarLayout;    // EnemyBarLayout
extern int enemyBarPosition;  // EnemyBarPosition
extern int enemyBarOpacity;   // 0..100 (percent; 0 hides the bars)
extern bool smoothMotion;   // interpolate motion between logic ticks for smooth high-refresh play
void set_smooth_motion(bool enabled);  // off: disables supersampling; off->on: Sub-pixel back to Auto
extern bool extraSparks;    // raise the explosion superspark limit far above the classic 101 cap
// Superspark projectile trails (menu: Enhancements -> Weapons -> Spark Trails). Only
// Ep4/5 item data tags these projectiles; JE_applySuperSparks retags them per episode.
enum
{
	SUPER_SPARKS_AUTO = 0,  // no trail in ep1-3, trail in ep4/5 (vanilla per-episode)
	SUPER_SPARKS_ON,        // spark trail in every episode (ep4/5 behavior everywhere)
	SUPER_SPARKS_OFF,       // no spark trail in any episode (ep1-3 behavior everywhere)
	SUPER_SPARKS_COUNT
};
// Ice Beam and Ice Blast share the same spark-tagged projectile sprite.
typedef enum
{
	SSW_MEGA_PULSE = 0,  // Mega Pulse front gun (port 19, wpns 400-410; sprite 35, spark bank 7)
	SSW_WALLOP_BEAM,     // Beno Wallop Beam sidekick (wpn 736; sprites 30/29, bank 7)
	SSW_PROTRON_B,       // Beno Protron System -B- sidekick (wpn 737; sprite 28, bank 9)
	SSW_ICE,             // Ice Beam + Ice Blast specials (wpns 621/706; sprite 634, bank 9)
	SSW_COUNT
} SuperSparkWeapon;
extern int  superSparkMode[SSW_COUNT];       // SUPER_SPARKS_* : where each weapon leaves its trail
extern bool superSparkClassicCap[SSW_COUNT]; // cap that trail at the classic limit even when extraSparks is on
bool superSparkCapForSprite(JE_word sprite); // cap setting for a trail-tagged shot sprite (JE_doSP calls in shots.c)
// Override the episode 4/5 Wallop Beam double-bolt pattern.
extern int wallopSecondBolt;

// Item-table differences exposed by the Episode Versions menu.
enum
{
	EPDIFF_AUTO = 0,     // per-episode default: ep1-3 data in ep1-3, ep4/5 data in ep4/5
	EPDIFF_EP13,         // force the ep1-3 behavior in every episode
	EPDIFF_EP45,         // force the ep4/5 behavior in every episode
	EPDIFF_MODE_COUNT
};
typedef enum
{
	EDW_XEGA_BALL = 0,   // Xega Ball special (wpn 720): ep1-3 two weak balls vs ep4/5 one strong ball
	EDW_MICROSOL_OPT5,   // MicroSol Option 5 (wpn 23): ep1-3 8-way fan vs ep4/5 twin shot
	EDW_FLARE,           // Flare / Super Bomb (wpn 622): blast sprite 20 (ep1-3) vs 21 (ep4/5)
	EDW_NEEDLE_LASER,    // Needle Laser (wpn 781): firing sound 31 vs 13
	EDW_BUBBLE_GUM,      // Bubble Gum-Gun (wpn 792): firing sound 30 vs 13
	EDW_FLYING_PUNCH,    // Flying Punch (wpn 794): firing sound 31 vs 30
	EDW_PRETZEL_MISSILE, // Pretzel Missile (wpn 795): firing sound 31 vs 30
	EDW_DRAGON_FROST,    // Dragon Frost (wpn 806): firing sound 31 vs 30
	EDW_SOLAR_SHIELD,    // Gencore Solar Shield (shield 8): shop icon 165 vs 153
	EDW_USHIP_PIC,       // U-Ship (ship 10): shop picture 28 (ep1-3) vs 32 (ep4/5)
	EDW_NORTSHIP_PIC,    // Nort Ship (ship 12): shop picture 33 (ep1-3) vs 32 (ep4/5)
	EDW_COUNT
} EpDiffWeapon;
extern int epDiffMode[EDW_COUNT];  // EPDIFF_* : which episode's data each item uses

// Per-gauge shade direction. Up matches the shipped HUD.
typedef enum
{
	GAUGE_GRAD_UP = 0,   // vertical, brightest at the top    (classic)
	GAUGE_GRAD_DOWN,     // vertical, brightest at the bottom
	GAUGE_GRAD_LEFT,     // horizontal, brightest at the left column
	GAUGE_GRAD_RIGHT,    // horizontal, brightest at the right column
	GAUGE_GRAD_COUNT
} GaugeGradientDir;

extern int gaugeGradGenerator;  // GaugeGradientDir for the generator power gauge
extern int gaugeGradShield;     // GaugeGradientDir for the shield gauge
extern int gaugeGradArmor;      // GaugeGradientDir for the armor gauge

extern bool gaugeFlashShield;
extern bool gaugeFlashArmor;

// Presentation-only full-screen tint for flare-family specials.
extern bool specialScreenTint;

// Zica level 11 settings: shot layout, bolt length, and an optional level 10 beam.
// Auto, Short, and off preserve the episode defaults.
enum
{
	ZICA_BASE_AUTO = 0,  // per-episode default: ep1-3 columns, ep4+ spread (vanilla)
	ZICA_BASE_EP13,      // force the ep1-3 two-column pattern in every episode
	ZICA_BASE_EP4,       // force the ep4 centred-spread pattern in every episode
	ZICA_BASE_COUNT
};
enum
{
	ZICA_LEN_SHORT = 0,  // vanilla fast bolts
	ZICA_LEN_LONG,       // beams as long as the Lv10 shot
	ZICA_LEN_COUNT
};

extern int  zicaLaserBase;      // ZICA_BASE_* : Lv11 horizontal shot pattern
extern int  zicaLaserLength;    // ZICA_LEN_*  : Lv11 shot length
extern bool zicaLaserLock;      // Length=Long: lock the side beams to the ship (like the Lv10 beam)
extern bool zicaLaserBuff;      // also fire the Lv10 beam alongside the Lv11 shots
extern bool chargeLaserCannon;  // re-add the cut DOS "Charge-Laser Cannon" sidekick to shops
// Wakes dormant dispenser bases 80-83. Endless rolls this per zone instead.
extern bool restoreBaseDispensers;
extern bool arcadeLifeBoost;    // arcade modes: lives scale the shield and armour ceilings (player.c)
extern bool arcadeRandomBalls;  // arcade modes: re-roll each weapon ball within its class (JE_makeEnemy, game_loop.c)
extern bool arcadeRearGunScale; // 1P arcade: lives raise the rear gun on top of its own pickups (player.c)
extern bool unusedShopSprites;  // give the shop sheet's unreferenced icons to items that share one (episodes.c)
extern bool centeredShotHitboxes;  // collide a projectile from the middle of its sprite, not its corner (game_loop.c)
extern bool guidedShotScreenAim;   // weapon-table homing steers toward an enemy's screen x, ex + mapoffset (shots.c)
extern bool shipEditorStars;
extern int  weaponEditorShip;
extern int  xmasMode;           // -1 = auto (by date), 0 = force off, 1 = force on

/* Enhancement presets cover enhancementSettings[] only. */
typedef enum
{
	ENH_PRESET_VANILLA = 0,  // reproduce the shipped DOS behavior wherever a setting can
	ENH_PRESET_ENGAGED,      // this fork's recommended set, which is also its defaults
	ENH_PRESET_CUSTOM,       // the live values match neither preset
	ENH_PRESET_COUNT
} EnhancementPreset;

const char *enhancementPresetName(EnhancementPreset preset);
// The preset the enhancement settings currently match.
EnhancementPreset enhancementPresetState(void);
/* Write the settings the named preset holds. CUSTOM hands back the set the player last had, and
 * does nothing when there is none to hand back. */
void enhancementApplyPreset(EnhancementPreset preset);
// Whether a Custom set has been remembered, and so whether CUSTOM is a preset that can be applied.
bool enhancementCustomAvailable(void);
/* Remember the live settings as the Custom set if they match neither preset. The menu calls this
 * as it runs, so any hand edit becomes the set that CUSTOM restores. */
void enhancementNoteCustom(void);

extern Config opentyrian_config;

void JE_initProcessorType(void);
void JE_setNewGameSpeed(void);
extern bool configuration_loaded;
void JE_loadConfiguration(void);
void JE_saveConfiguration(void);
bool save_opentyrian_config(void);  // write opentyrian.cfg now

#endif /* CONFIG_H */
