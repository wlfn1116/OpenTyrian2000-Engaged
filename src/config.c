/* 
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "config.h"

#include "console_platform.h"  // SWITCH_/VITA_USER_DIR
#include "crashlog.h"
#include "custom_weapon.h"
#include "endless.h"       // endlessDebugConfigLoad/Save ([endless_debug] section)
#include "episodes.h"
#include "file.h"
#include "helptext.h"      // DESTRUCT_MODES bounds the persisted battle mode
#include "joystick.h"
#include "keyboard.h"
#include "loudness.h"
#include "mtrand.h"
#include "net_rollback.h"
#include "network.h"
#include "nortsong.h"
#include "opentyr.h"
#include "params.h"        // constantPlay bars the co-op Campaign board
#include "player.h"
#include "rollback.h"
#include "varz.h"
#include "vga256d.h"
#include "video.h"
#include "video_scale.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#include <direct.h>
#define mkdir _mkdir
#else
#include <unistd.h>
#endif

/* Configuration Load/Save handler */

const JE_byte cryptKey[10] = /* [1..10] */
{
	15, 50, 89, 240, 147, 34, 86, 9, 32, 208
};

const DosKeySettings defaultDosKeySettings =
{
	72, 80, 75, 77, 57, 28, 29, 56
};

const MouseSettings defaultMouseSettings =
{
	1, 4, 5
};

const KeySettings defaultKeySettings =
{
	SDL_SCANCODE_UP,
	SDL_SCANCODE_DOWN,
	SDL_SCANCODE_LEFT,
	SDL_SCANCODE_RIGHT,
	SDL_SCANCODE_SPACE,
	SDL_SCANCODE_RETURN,
	SDL_SCANCODE_LCTRL,
	SDL_SCANCODE_LALT,
};

static const char *const keySettingNames[] =
{
	"up",
	"down",
	"left",
	"right",
	"fire",
	"change fire",
	"left sidekick",
	"right sidekick",
};

static const char *const mouseSettingNames[] =
{
	"left mouse",
	"right mouse",
	"middle mouse",
};

static const char *const mouseSettingValues[] =
{
	"fire main weapon",
	"fire left sidekick",
	"fire right sidekick",
	"fire both sidekicks",
	"change rear mode",
};

char defaultHighScoreNames[39][23]; /* [1..39] of string [22] */
char defaultTeamNames[10][25]; /* [1..22] of string [24] */

const JE_EditorItemAvailType initialItemAvail =
{
	1,1,1,0,0,1,1,0,1,1,1,1,1,0,1,0,1,1,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0, /* Front/Rear Weapons 1-38  */
	0,0,0,0,0,0,0,0,0,0,1,                                                           /* Fill                     */
	1,0,0,0,0,1,0,0,0,1,1,0,1,0,0,0,0,0,                                             /* Sidekicks          51-68 */
	0,0,0,0,0,0,0,0,0,0,0,                                                           /* Fill                     */
	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                                                   /* Special Weapons    81-93 */
	0,0,0,0,0                                                                        /* Fill                     */
};

/* Last 2 bytes = Word
 *
 * Max Value = 1680
 * X div  60 = Armor  (1-28)
 * X div 168 = Shield (1-12)
 * X div 280 = Engine (1-06)
 */

JE_boolean smoothies[9] = /* [1..9] */
{ 0, 0, 0, 0, 0, 0, 0, 0, 0 };

JE_byte starShowVGASpecialCode;

/* CubeData */
JE_word lastCubeMax, cubeMax;
JE_word cubeList[4]; /* [1..4] */

/* High-Score Stuff */
JE_boolean gameHasRepeated;  // can only get highscore on first play-through

/* Difficulty */
JE_shortint difficultyLevel, oldDifficultyLevel,
            initialDifficulty;  // can only get highscore on initial episode

/* Timed Battle */
JE_byte timeBattleSelection;

/* Player Stuff */
uint    power, lastPower, powerAdd;
JE_byte shieldWait, shieldT;

JE_byte          shotRepeat[11], shotMultiPos[11];
JE_boolean       portConfigChange, portConfigDone;

/* Level Data */
char    lastLevelName[11], levelName[11]; /* string [10] */
JE_byte mainLevel, nextLevel, saveLevel;   /*Current Level #*/

/* Keyboard Junk */
DosKeySettings dosKeySettings;
KeySettings keySettings;

/* Mouse settings */
MouseSettings mouseSettings;

/* Configuration */
JE_shortint levelFilter, levelFilterNew, levelBrightness, levelBrightnessChg;
JE_boolean  filtrationAvail, filterActive, filterFade, filterFadeStart;

JE_boolean gameJustLoaded;

JE_boolean galagaMode;

JE_boolean extraGame;

JE_boolean engageMode;

JE_boolean twoPlayerMode, twoPlayerLinked, onePlayerAction, timedBattleMode, superTyrian;
JE_boolean coopCampaignMode;
JE_boolean arcadeSeparateMode;
JE_boolean coopEndlessMode;
JE_boolean endlessMode;  // Endless roguelite mode (see endless.c)
JE_boolean endlessCampaignMods;  // debug: endless EFFECTS in a normal game (see endlessFxActive)
JE_boolean trentWin = false;
JE_byte    superArcadeMode;

JE_byte    superArcadePowerUp;

JE_real linkGunDirec;
JE_byte inputDevice[2] = { 1, 2 }; // 0:any  1:keyboard  2:mouse  3+:joystick

JE_byte secretHint;
JE_byte background3over;
JE_byte background2over;
JE_byte gammaCorrection;
JE_boolean superPause = false;
JE_boolean explosionTransparent,
           youAreCheating,
           displayScore,
           background2, smoothScroll, wild, superWild, starActive,
           topEnemyOver,
           skyEnemyOverAll,
           background2notTransparent;

JE_byte soundEffects; // dummy value for config
JE_byte versionNum;   /* SW 1.0 and SW/Reg 1.1 = 0 or 1
                       * EA 1.2 = 2        T2K = 3*/

JE_byte    fastPlay;
JE_boolean pentiumMode;

/* Savegame files */
JE_byte    gameSpeed;
JE_byte    processorType;  /* detail level: 1=Low 2=Normal 3=High 4=Pentium 5=Laptop VGA 6=Wild */

JE_SaveFilesType saveFiles; /*array[1..saveLevelnum] of savefiletype;*/
JE_SaveGameTemp saveTemp;

T2KHighScoreType t2kHighScores[20][3];

COMPILE_TIME_ASSERT(coop_campaign_score_episodes, COOP_CAMPAIGN_SCORE_EPISODES == EPISODE_MAX);
CoopCampaignScore coopCampaignScores[COOP_CAMPAIGN_SCORE_EPISODES];

const char *coopCampaignCreditName(Uint8 credit)
{
	switch (credit)
	{
	case COOP_CREDIT_SHARED:             return "Shared";
	case COOP_CREDIT_INDIVIDUAL:         return "Individual";
	case COOP_CREDIT_INDIVIDUAL_DOUBLED: return "Individual x2";
	default:                             return NULL;
	}
}

/* One line per episode: "score|difficulty|names". A missing or short value leaves the rest of
 * the episodes empty, so the list can grow. The credit rule rides a second key rather than a
 * fourth field, because the name runs to the end of the line and may itself contain a bar. */
void coopCampaignScoreConfigSave(ConfigSection *section)
{
	if (section == NULL)
		return;

	for (int e = 0; e < COOP_CAMPAIGN_SCORE_EPISODES; ++e)
	{
		char key[32], line[64];
		snprintf(key, sizeof(key), "coop_campaign_%d", e + 1);
		snprintf(line, sizeof(line), "%d|%u|%s", coopCampaignScores[e].score,
		         (unsigned)coopCampaignScores[e].difficulty, coopCampaignScores[e].name);
		config_set_string_option(section, key, line);

		snprintf(key, sizeof(key), "coop_campaign_credit_%d", e + 1);
		config_set_int_option(section, key, coopCampaignScores[e].credit);
	}
}

void coopCampaignScoreConfigLoad(const ConfigSection *section)
{
	memset(coopCampaignScores, 0, sizeof(coopCampaignScores));
	if (section == NULL)
		return;

	for (int e = 0; e < COOP_CAMPAIGN_SCORE_EPISODES; ++e)
	{
		char key[32];
		snprintf(key, sizeof(key), "coop_campaign_%d", e + 1);

		const char *line = NULL;
		if (!config_get_string_option(section, key, &line) || line == NULL)
			continue;

		const long score = strtol(line, NULL, 10);
		coopCampaignScores[e].score = (score > 0) ? (Sint32)score : 0;

		const char *p = strchr(line, '|');
		if (p == NULL)
			continue;
		const long diff = strtol(p + 1, NULL, 10);
		coopCampaignScores[e].difficulty = (diff > 0 && diff <= DIFFICULTY_10) ? (Uint8)diff : 0;

		p = strchr(p + 1, '|');
		if (p != NULL)
			SDL_strlcpy(coopCampaignScores[e].name, p + 1, sizeof(coopCampaignScores[e].name));

		snprintf(key, sizeof(key), "coop_campaign_credit_%d", e + 1);
		int credit = COOP_CREDIT_UNKNOWN;
		if (config_get_int_option(section, key, &credit)
		    && credit > COOP_CREDIT_UNKNOWN && credit < COOP_CREDIT_COUNT)
			coopCampaignScores[e].credit = (Uint8)credit;
	}
}

/* Records a completed co-op Campaign episode. The pair earned together, so the board keeps their
 * combined cash under both names; there is no name-entry dialog because the lobby already knows
 * who they are, and a modal here would leave the other machine waiting on a dead screen. Every
 * condition for a record lives here rather than at the call site: see "Online Campaign records"
 * in doc/notes.md. */
void coopCampaignScoreNote(void)
{
	const int e = initial_episode_num - 1;
	if (!coopCampaignMode || constantPlay || e < 0 || e >= COOP_CAMPAIGN_SCORE_EPISODES)
		return;

	// Only the episode the run started in, played once: a later episode or a repeat carries cash
	// earned before it into this episode's record.
	if (episodeNum != initial_episode_num || gameHasRepeated)
		return;

	const Sint64 total = (Sint64)player[0].cash + (Sint64)player[1].cash;
	if (total <= coopCampaignScores[e].score)
		return;

	coopCampaignScores[e].score = (total > 0x7fffffff) ? 0x7fffffff : (Sint32)total;
	coopCampaignScores[e].difficulty = (Uint8)initialDifficulty;
	if (coop_credit_is_shared())
		coopCampaignScores[e].credit = COOP_CREDIT_SHARED;
	else if (coop_earnings_are_doubled())
		coopCampaignScores[e].credit = COOP_CREDIT_INDIVIDUAL_DOUBLED;
	else
		coopCampaignScores[e].credit = COOP_CREDIT_INDIVIDUAL;

	const char *const mine = (network_player_name != NULL && network_player_name[0])
	                       ? network_player_name : "Player 1";
	const char *const theirs = (network_opponent_name != NULL && network_opponent_name[0])
	                         ? network_opponent_name : "Player 2";
	snprintf(coopCampaignScores[e].name, sizeof(coopCampaignScores[e].name), "%s and %s", mine, theirs);

	save_opentyrian_config();
}

/* One bit per two-player slot: set when this machine wrote that slot while flying player two.
 * See save_slot_online_player in config.h; `net_save_player_two` carries it in opentyrian.cfg. */
static Uint16 saveSlotPlayerTwo;

// Slots 1 through 11 are the one-player page, which no online session writes.
static uint save_slot_online_bit(JE_byte slot)
{
	return (slot >= 12 && slot <= SAVE_FILES_NUM) ? 1u << (slot - 12) : 0u;
}

uint save_slot_online_player(JE_byte slot)
{
	const uint bit = save_slot_online_bit(slot);
	return (bit != 0 && (saveSlotPlayerTwo & bit) != 0) ? 2u : 1u;
}

void save_slot_set_online_player(JE_byte slot, uint playerNum)
{
	const uint bit = save_slot_online_bit(slot);
	if (bit == 0)
		return;

	if (playerNum == 2)
		saveSlotPlayerTwo |= (Uint16)bit;
	else
		saveSlotPlayerTwo &= (Uint16)~bit;
}

JE_word editorLevel;   /*Initial value 800*/

/* Enhancement settings (persisted in the [enhancements] config section). */
int bossBarStyle   = BOSS_BAR_ENHANCED;
int bossBarLayout  = BOSS_BAR_TOP;
int bossBarTwoMode = BOSS_BAR_TWO_SPLIT;
bool armorAlarm    = true;  // low-armor WARNING siren (Setup > Sound)
bool linkSounds    = true;  // 2P fuse/unfuse clink+spring (Setup > Sound)
/* When off: debug menu and debug level select hidden; buy/sell and pause menus
   keep their stock layout. */
bool debugMode     = true;
/* Extends horizontal parallax to the full map span. Off preserves the stock amplitude. */
bool extraParallax = false;
/* Reflects columns beyond a map row's edge in both parallax modes. */
bool mirroredLayers = true;
/* Thin health bar near an enemy once damaged (draw_enemy_health_bars in tyrian2.c). */
bool enemyBars       = true;
int enemyBarLayout   = ENEMY_BAR_HORIZONTAL;
int enemyBarPosition = ENEMY_BAR_POS_BOTTOM;
int enemyBarOpacity  = 75;
/* Interpolate motion between the fixed ~35Hz logic ticks for high-refresh play;
   off = classic one present per tick. Gates vt_ship_owns() and the JE_starShowVGA
   interpolation loop (tyrian2.c). */
bool smoothMotion  = true;

void set_smooth_motion(bool enabled)
{
	// Only on a real off->on transition (not the config-load self-call), so a
	// deliberately saved Sub-pixel choice survives a restart.
	if (enabled && !smoothMotion)
		render_supersample = 0;  // re-arm Auto supersampling
	smoothMotion = enabled;
	if (!smoothMotion)
		render_supersample = 1;
}

/* Bigger explosion "superspark" ring buffer (MAX_SUPERPIXELS) vs the classic 101-spark cap;
   off = the original sparser DOS spark showers. Read by JE_doSP (varz.c). */
bool extraSparks = true;
/* Where each superspark weapon leaves its ep4/5 projectile trail (JE_applySuperSparks,
   episodes.c): SUPER_SPARKS_AUTO (vanilla per-episode), _ON (every episode), _OFF (no episode).
   Default On so the trails show in ep1-3 as well (matches the original Mega Pulse request). */
int superSparkMode[SSW_COUNT] = { SUPER_SPARKS_ON, SUPER_SPARKS_ON, SUPER_SPARKS_ON, SUPER_SPARKS_ON };
/* Cap a weapon's projectile trail at the classic 101-spark limit even when extraSparks is
   on, so the trail keeps its classic density (JE_doSP calls in shots.c). On by default. */
bool superSparkClassicCap[SSW_COUNT] = { true, true, true, true };
/* The ep4/5 Wallop Beam's second bolt (JE_applySuperSparks, episodes.c). Unlike the spark
   trails this changes firepower (two bolts per volley); On gives every episode the ep4/5
   double-bolt pattern. */
int wallopSecondBolt = SUPER_SPARKS_ON;

/* Config keys for the per-weapon trail settings; indexed by SuperSparkWeapon. */
static const char *const superSparkKeys[SSW_COUNT]    = { "superspark_mega_pulse", "superspark_wallop_beam", "superspark_protron_b", "superspark_ice" };
static const char *const superSparkCapKeys[SSW_COUNT] = { "superspark_mega_pulse_cap", "superspark_wallop_beam_cap", "superspark_protron_b_cap", "superspark_ice_cap" };

/* Which episode's data each non-spark difference item uses (JE_applyEpDiffs, episodes.c):
   EPDIFF_AUTO (per-episode, vanilla), _EP13, or _EP45. The three gameplay reworks stay on Auto,
   so each episode plays its own. The rest are picked one by one for how they sound and look
   rather than by era, so the two shop ship pictures and the Gum-Gun keep their ep1-3 version
   while the other four firing sounds and the Solar Shield icon take the ep4/5 one.
   Keep in EpDiffWeapon order. */
int epDiffMode[EDW_COUNT] = {
	EPDIFF_AUTO, EPDIFF_AUTO, EPDIFF_AUTO,                                // gameplay reworks
	EPDIFF_EP45, EPDIFF_EP13, EPDIFF_EP45, EPDIFF_EP45, EPDIFF_EP45,      // firing sounds
	EPDIFF_EP45,                                                          // Solar Shield icon
	EPDIFF_EP13, EPDIFF_EP13                                              // borrowed ship pictures
};
/* Config keys for the per-item episode-difference settings; indexed by EpDiffWeapon. */
static const char *const epDiffKeys[EDW_COUNT] = {
	"epdiff_xega_ball", "epdiff_microsol_opt5", "epdiff_flare", "epdiff_needle_laser",
	"epdiff_bubble_gum", "epdiff_flying_punch", "epdiff_pretzel_missile", "epdiff_dragon_frost",
	"epdiff_solar_shield", "epdiff_uship_picture", "epdiff_nortship_picture"
};

/* Map a trail-tagged shot's sprite back to its weapon's cap setting, for the JE_doSP calls
   in shots.c (a flying shot only knows its graphic). Pass the shot's base graphic (shotGr),
   never the drawn frame: shotGr + shotAni walks past the entry on every animated frame.
   Unknown sprites (e.g. sparky custom weapon bullets) honor extraSparks uncapped. */
bool superSparkCapForSprite(JE_word sprite)
{
	switch (sprite)
	{
	case 35:           return superSparkClassicCap[SSW_MEGA_PULSE];
	case 30: case 29:  return superSparkClassicCap[SSW_WALLOP_BEAM];
	case 28:           return superSparkClassicCap[SSW_PROTRON_B];
	case 634:          return superSparkClassicCap[SSW_ICE];
	default:           return false;
	}
}
/* HUD gauge gradient direction (Enhancements -> Heads-Up Display -> Gauges). GAUGE_GRAD_UP
   reproduces the classic vertical gauges; other values run the gradient down the column or
   across its width. Read by draw_power_gauge (tyrian2.c) and JE_dBar3 (nortvars.c). */
int gaugeGradGenerator = GAUGE_GRAD_UP;
int gaugeGradShield    = GAUGE_GRAD_RIGHT;
int gaugeGradArmor     = GAUGE_GRAD_LEFT;
bool gaugeFlashShield  = true;
bool gaugeFlashArmor   = true;
/* Let a burning flare special grade the whole screen its colour, as shipped (the paint is in
   tyrian2.c; the flare that installs it is JE_doSpecialShot in varz.c). */
bool specialScreenTint = true;
/* Zica Laser Lv11 tweaks (JE_applyZicaLaserConfig in episodes.c; front-weapon fire
   loop in mainint.c). */
int zicaLaserBase = ZICA_BASE_EP4;      /* ZICA_BASE_*: Lv11 shot pattern */
int zicaLaserLength = ZICA_LEN_SHORT;   /* ZICA_LEN_* : Lv11 shot length */
bool zicaLaserLock = false;             /* Length=Long: ship-lock the side beams (default = free) */
bool zicaLaserBuff = true;              /* also fire the Lv10 beam alongside the Lv11 shots */
/* Re-add the cut DOS "Charge-Laser Cannon" sidekick to its original shops + the debug
   menu (JE_applyChargeLaserCannon in episodes.c). */
bool chargeLaserCannon = true;
/* Wake the dormant dispenser bases (enemy 80-83; JE_makeEnemy in tyrian2.c). Campaign
   only; Endless ignores the toggle and asks the zone instead. */
bool restoreBaseDispensers = true;
/* Arcade modes only: a ship's shield and armour ceilings scale with its life count
   (arcade_life_scaling_active in player.c). Off leaves the vanilla hull numbers. */
bool arcadeLifeBoost = true;
/* Arcade modes only: every weapon ball a level drops is re-rolled inside its own class
   (JE_makeEnemy in tyrian2.c). Off = the hand-placed pickups the level scripts specify. */
bool arcadeRandomBalls = true;
/* One-player arcade only: the life count raises the rear gun on top of its own banked power-up
   balls, instead of the rear gun sitting where those balls left it (arcade_weapon_power in
   player.c). Two-player is out; there the rear bay already IS player 2's life counter. */
bool arcadeRearGunScale = true;
/* Spend the sheets' never-referenced icons on the weapons, sidekicks and specials that ship
   sharing another item's icon or with none at all (JE_applyUnusedShopSprites in episodes.c).
   Cosmetic; it shows in the shops and on the special-weapon HUD icon, and matters most in
   Endless, which offers every port at once. */
bool unusedShopSprites = true;
/* Take a projectile's hit test from the middle of its sprite rather than the top-left corner of
   its cell, and the target's from the middle of its own (the two shot loops in tyrian2.c). Off
   restores the vanilla geometry, whose boxes sit above the sprites they belong to.
   Host-authoritative online; doc/notes.md has the box arithmetic. */
bool centeredShotHitboxes = true;
/* Steer a weapon-table guided shot toward the enemy's screen x (ex + mapoffset), where the
   collision loop measures it (player_shot_aim_step in shots.c). Off keeps the shipped map-x aim,
   which the attract demos and the replay fixtures pin. Host-authoritative online; doc/notes.md,
   "Combat", covers what stays stock. */
bool guidedShotScreenAim = false;
/* Christmas mode override: -1 = auto-detect by date (original), 0 = force off, 1 = force
   on. Set to 0/1 by the Enhancements toggle so the choice persists. */
int xmasMode = 0;

/* Every setting the Enhancements menu edits, with the value each preset writes to it. Vanilla
 * reproduces the DOS game wherever a setting can; Engaged is this fork's recommended set and
 * matches the defaults above. See "Menus and UI" in doc/notes.md for what depends on this table
 * staying complete. */
typedef struct
{
	int  *intSetting;   // exactly one of the two pointers is set
	bool *boolSetting;
	int   vanilla;
	int   engaged;
} EnhancementSetting;

static const EnhancementSetting enhancementSettings[] = {
	/* Visuals. */
	{ .boolSetting = &extraParallax,     .vanilla = false, .engaged = false },
	{ .boolSetting = &mirroredLayers,    .vanilla = false, .engaged = true },
	{ .boolSetting = &extraSparks,       .vanilla = false, .engaged = true },
	{ .boolSetting = &specialScreenTint, .vanilla = true,  .engaged = true },
	{ .boolSetting = &unusedShopSprites, .vanilla = false, .engaged = true },

	/* Enemy bars. Layout, position and opacity only show once the bars are on, so both
	 * presets share them. */
	{ .boolSetting = &enemyBars,       .vanilla = false, .engaged = true },
	{ .intSetting = &enemyBarLayout,   .vanilla = ENEMY_BAR_HORIZONTAL, .engaged = ENEMY_BAR_HORIZONTAL },
	{ .intSetting = &enemyBarPosition, .vanilla = ENEMY_BAR_POS_BOTTOM, .engaged = ENEMY_BAR_POS_BOTTOM },
	{ .intSetting = &enemyBarOpacity,  .vanilla = 75, .engaged = 75 },

	/* Boss bars. Classic bars ignore layout and grouping, so both presets leave those alone. */
	{ .intSetting = &bossBarStyle,   .vanilla = BOSS_BAR_CLASSIC,   .engaged = BOSS_BAR_ENHANCED },
	{ .intSetting = &bossBarLayout,  .vanilla = BOSS_BAR_TOP,       .engaged = BOSS_BAR_TOP },
	{ .intSetting = &bossBarTwoMode, .vanilla = BOSS_BAR_TWO_SPLIT, .engaged = BOSS_BAR_TWO_SPLIT },

	/* Gauges. */
	{ .intSetting = &gaugeGradGenerator, .vanilla = GAUGE_GRAD_UP, .engaged = GAUGE_GRAD_UP },
	{ .intSetting = &gaugeGradShield,    .vanilla = GAUGE_GRAD_UP, .engaged = GAUGE_GRAD_RIGHT },
	{ .intSetting = &gaugeGradArmor,     .vanilla = GAUGE_GRAD_UP, .engaged = GAUGE_GRAD_LEFT },
	{ .boolSetting = &gaugeFlashShield,  .vanilla = false,         .engaged = true },
	{ .boolSetting = &gaugeFlashArmor,   .vanilla = false,         .engaged = true },

	/* Weapons. */
	{ .boolSetting = &customWeaponEnabled, .vanilla = false, .engaged = true },
	{ .boolSetting = &chargeLaserCannon,   .vanilla = false, .engaged = true },

	/* Spark trails, and the Wallop second bolt that goes with them. */
	{ .intSetting = &superSparkMode[SSW_MEGA_PULSE],  .vanilla = SUPER_SPARKS_AUTO, .engaged = SUPER_SPARKS_ON },
	{ .intSetting = &superSparkMode[SSW_WALLOP_BEAM], .vanilla = SUPER_SPARKS_AUTO, .engaged = SUPER_SPARKS_ON },
	{ .intSetting = &superSparkMode[SSW_PROTRON_B],   .vanilla = SUPER_SPARKS_AUTO, .engaged = SUPER_SPARKS_ON },
	{ .intSetting = &superSparkMode[SSW_ICE],         .vanilla = SUPER_SPARKS_AUTO, .engaged = SUPER_SPARKS_ON },
	{ .intSetting = &wallopSecondBolt, .vanilla = SUPER_SPARKS_AUTO, .engaged = SUPER_SPARKS_ON },

	/* Classic spark caps. */
	{ .boolSetting = &superSparkClassicCap[SSW_MEGA_PULSE],  .vanilla = true, .engaged = true },
	{ .boolSetting = &superSparkClassicCap[SSW_WALLOP_BEAM], .vanilla = true, .engaged = true },
	{ .boolSetting = &superSparkClassicCap[SSW_PROTRON_B],   .vanilla = true, .engaged = true },
	{ .boolSetting = &superSparkClassicCap[SSW_ICE],         .vanilla = true, .engaged = true },

	/* Gameplay. Guided Aim is an opt-in: both presets keep the shipped homing. */
	{ .boolSetting = &centeredShotHitboxes,  .vanilla = false, .engaged = true },
	{ .boolSetting = &guidedShotScreenAim,   .vanilla = false, .engaged = false },
	{ .boolSetting = &restoreBaseDispensers, .vanilla = false, .engaged = true },

	/* Arcade modes. */
	{ .boolSetting = &arcadeLifeBoost,    .vanilla = false, .engaged = true },
	{ .boolSetting = &arcadeRandomBalls,  .vanilla = false, .engaged = true },
	{ .boolSetting = &arcadeRearGunScale, .vanilla = false, .engaged = true },

	/* Zica Laser Lv11. */
	{ .intSetting = &zicaLaserBase,   .vanilla = ZICA_BASE_AUTO, .engaged = ZICA_BASE_EP4 },
	{ .intSetting = &zicaLaserLength, .vanilla = ZICA_LEN_SHORT, .engaged = ZICA_LEN_SHORT },
	{ .boolSetting = &zicaLaserLock,  .vanilla = false,          .engaged = false },
	{ .boolSetting = &zicaLaserBuff,  .vanilla = false,          .engaged = true },

	/* Episode item data. Vanilla is Auto throughout, which plays each episode with the data it
	 * shipped with. Engaged keeps Auto for the three gameplay reworks and pins the presentation
	 * rows one by one, for how each sounds or looks rather than by era. */
	{ .intSetting = &epDiffMode[EDW_XEGA_BALL],       .vanilla = EPDIFF_AUTO, .engaged = EPDIFF_AUTO },
	{ .intSetting = &epDiffMode[EDW_MICROSOL_OPT5],   .vanilla = EPDIFF_AUTO, .engaged = EPDIFF_AUTO },
	{ .intSetting = &epDiffMode[EDW_FLARE],           .vanilla = EPDIFF_AUTO, .engaged = EPDIFF_AUTO },
	{ .intSetting = &epDiffMode[EDW_NEEDLE_LASER],    .vanilla = EPDIFF_AUTO, .engaged = EPDIFF_EP45 },
	{ .intSetting = &epDiffMode[EDW_BUBBLE_GUM],      .vanilla = EPDIFF_AUTO, .engaged = EPDIFF_EP13 },
	{ .intSetting = &epDiffMode[EDW_FLYING_PUNCH],    .vanilla = EPDIFF_AUTO, .engaged = EPDIFF_EP45 },
	{ .intSetting = &epDiffMode[EDW_PRETZEL_MISSILE], .vanilla = EPDIFF_AUTO, .engaged = EPDIFF_EP45 },
	{ .intSetting = &epDiffMode[EDW_DRAGON_FROST],    .vanilla = EPDIFF_AUTO, .engaged = EPDIFF_EP45 },
	{ .intSetting = &epDiffMode[EDW_SOLAR_SHIELD],    .vanilla = EPDIFF_AUTO, .engaged = EPDIFF_EP45 },
	{ .intSetting = &epDiffMode[EDW_USHIP_PIC],       .vanilla = EPDIFF_AUTO, .engaged = EPDIFF_EP13 },
	{ .intSetting = &epDiffMode[EDW_NORTSHIP_PIC],    .vanilla = EPDIFF_AUTO, .engaged = EPDIFF_EP13 },
};

static int enhancementRead(const EnhancementSetting *setting)
{
	return setting->intSetting != NULL ? *setting->intSetting : (*setting->boolSetting ? 1 : 0);
}

#define PRESET_BIT(preset) (1u << (preset))

const char *enhancementPresetName(EnhancementPreset preset)
{
	static const char *const names[ENH_PRESET_COUNT] = { "Vanilla", "Engaged", "Custom" };

	return names[preset % ENH_PRESET_COUNT];
}

EnhancementPreset enhancementPresetState(void)
{
	unsigned int match = PRESET_BIT(ENH_PRESET_VANILLA) | PRESET_BIT(ENH_PRESET_ENGAGED);

	for (size_t i = 0; i < COUNTOF(enhancementSettings); ++i)
	{
		const EnhancementSetting *const setting = &enhancementSettings[i];
		const int value = enhancementRead(setting);

		if (value != setting->vanilla)
			match &= ~PRESET_BIT(ENH_PRESET_VANILLA);
		if (value != setting->engaged)
			match &= ~PRESET_BIT(ENH_PRESET_ENGAGED);
	}

	// The two presets differ somewhere, so at most one of these bits survives.
	if ((match & PRESET_BIT(ENH_PRESET_ENGAGED)) != 0)
		return ENH_PRESET_ENGAGED;
	if ((match & PRESET_BIT(ENH_PRESET_VANILLA)) != 0)
		return ENH_PRESET_VANILLA;

	return ENH_PRESET_CUSTOM;
}

/* The set the player built by hand, so switching to Vanilla or Engaged and back hands it over
 * again rather than losing it. Captured whenever the live settings match neither preset. */
static int enhancementCustom[COUNTOF(enhancementSettings)];
static bool enhancementCustomKnown = false;

/* Fingerprint of the table's fixed columns. A stored Custom set is a positional list, so it is
 * only meaningful against the table it was written from; a reordered or retuned table changes
 * this and the stored list is dropped instead of restoring values into the wrong settings. */
static unsigned int enhancementTableShape(void)
{
	unsigned int shape = (unsigned int)COUNTOF(enhancementSettings);

	for (size_t i = 0; i < COUNTOF(enhancementSettings); ++i)
	{
		const EnhancementSetting *const setting = &enhancementSettings[i];
		const unsigned int row = (unsigned int)(setting->vanilla * 3 + setting->engaged * 5)
		                       + (setting->intSetting != NULL ? 1u : 0u);

		shape = (shape ^ row) * 16777619u;  // FNV-1a mixing step
	}

	return shape;
}

/* Read back a stored Custom set. It is a positional list against the table above, so anything
 * short of a full list for this table leaves nothing remembered rather than a partial set. */
static void enhancementLoadCustomSet(const char *list)
{
	int values[COUNTOF(enhancementSettings)];
	size_t count = 0;

	for (const char *p = list; *p != '\0'; )
	{
		char *end;
		const long value = strtol(p, &end, 10);

		if (end == p || count == COUNTOF(values))
			return;

		values[count++] = (int)value;
		p = (*end == ',') ? end + 1 : end;
	}

	if (count != COUNTOF(values))
		return;

	memcpy(enhancementCustom, values, sizeof(enhancementCustom));
	enhancementCustomKnown = true;
}

static void enhancementSaveCustomSet(ConfigSection *section)
{
	if (!enhancementCustomKnown)
		return;

	char list[COUNTOF(enhancementSettings) * 8];
	size_t used = 0;

	for (size_t i = 0; i < COUNTOF(enhancementSettings); ++i)
	{
		const int written = snprintf(list + used, sizeof(list) - used, "%s%d",
		                             i == 0 ? "" : ",", enhancementCustom[i]);
		if (written < 0 || (size_t)written >= sizeof(list) - used)
			return;  // a truncated list would restore the wrong settings; write none

		used += (size_t)written;
	}

	config_set_string_option(section, "custom_set", list);
	config_set_int_option(section, "custom_set_shape", (int)enhancementTableShape());
}

bool enhancementCustomAvailable(void)
{
	return enhancementCustomKnown;
}

void enhancementNoteCustom(void)
{
	if (enhancementPresetState() != ENH_PRESET_CUSTOM)
		return;

	for (size_t i = 0; i < COUNTOF(enhancementSettings); ++i)
		enhancementCustom[i] = enhancementRead(&enhancementSettings[i]);

	enhancementCustomKnown = true;
}

void enhancementApplyPreset(EnhancementPreset preset)
{
	if (preset >= ENH_PRESET_COUNT)
		return;
	if (preset == ENH_PRESET_CUSTOM && !enhancementCustomKnown)
		return;

	for (size_t i = 0; i < COUNTOF(enhancementSettings); ++i)
	{
		const EnhancementSetting *const setting = &enhancementSettings[i];
		const int value = preset == ENH_PRESET_VANILLA ? setting->vanilla
		                : preset == ENH_PRESET_ENGAGED ? setting->engaged
		                                               : enhancementCustom[i];

		if (setting->intSetting != NULL)
			*setting->intSetting = value;
		else
			*setting->boolSetting = (value != 0);
	}

	// Rewrite the item data from the settings just written, the same way a hand-edited row does.
	JE_applyItemDataSettings();
}

Config opentyrian_config;  // implicitly initialized

// The custom weapon is persisted as a raw JE_WeaponType per power level (a compact
// comma-separated integer blob built by customWeaponSerializeLevel), plus the shared
// weapon-wide identity keys. See custom_weapon.c for the blob layout.

bool load_opentyrian_config(void)
{
	// defaults
	fullscreen_display = -1;
#ifdef __vita__
	// Vita's SGX GPU + A9 CPU can't afford a software-upscaled present every frame; present at
	// native size and let the GPU scale to the 960x544 panel.
	set_scaler_by_name("None");
#else
	set_scaler_by_name("4x");  // first-boot default: plain nearest-neighbour 4x (crisp pixels)
#endif
	memcpy(keySettings, defaultKeySettings, sizeof(keySettings));
	memcpy(mouseSettings, defaultMouseSettings, sizeof(mouseSettings));
	
	Config *config = &opentyrian_config;
	
	FILE *file = dir_fopen_warn(get_user_directory(), "opentyrian.cfg", "r");
	if (file == NULL)
		return false;

	if (!config_parse(config, file))
	{
		fclose(file);
		
		return false;
	}
	
	ConfigSection *section;
	
	section = config_find_section(config, "video", NULL);
	if (section != NULL)
	{
		config_get_int_option(section, "fullscreen", &fullscreen_display);

		const char* scaler_name;
		if (config_get_string_option(section, "scaler", &scaler_name))
			set_scaler_by_name(scaler_name);
#ifdef __vita__
		// Ignore any saved scaler on Vita: software upscaling every frame is too slow for the
		// hardware regardless of what a prior session wrote. Native + GPU upscale only. The
		// in-session Graphics menu can still change it to experiment, but each boot resets here.
		set_scaler_by_name("None");
#endif

		const char* scaling_mode;
		if (config_get_string_option(section, "scaling_mode", &scaling_mode))
			set_scaling_mode_by_name(scaling_mode);

		config_get_int_option(section, "fps", &fps_cap);
		set_fps(fps_cap);

		int vsync_enabled = output_vsync ? 1 : 0;
		config_get_int_option(section, "vsync", &vsync_enabled);
		set_vsync(vsync_enabled != 0);

		int show_fps_enabled = show_fps ? 1 : 0;
		config_get_int_option(section, "show_fps", &show_fps_enabled);
		show_fps = (show_fps_enabled != 0);

		// Ship-control sensitivity slider: touch on the consoles, mouse on desktop.
		config_get_int_option(section, SHIP_SENS_CFG, &ship_sensitivity);
		if (ship_sensitivity < 0 || ship_sensitivity > SHIP_SENS_MAX)
			ship_sensitivity = SHIP_SENS_DEFAULT;

		// Sub-pixel supersampling: 0 = Auto, 1 = off, 2..5 fixed, 6 = Native (match the display).
		config_get_int_option(section, "render_supersample", &render_supersample);
		if (render_supersample < 0)
			render_supersample = 0;
		else if (render_supersample > RENDER_SUPERSAMPLE_NATIVE)
			render_supersample = RENDER_SUPERSAMPLE_NATIVE;

		int smoothie_full = smoothie_full_res ? 1 : 0;
		config_get_int_option(section, "smoothie_full_res", &smoothie_full);
		smoothie_full_res = (smoothie_full != 0);

		/* Remove the retired filter setting when the configuration is next saved. */
		config_remove_option(section, "render_supersample_filter");
	}

	section = config_find_section(config, "keyboard", NULL);
	if (section != NULL)
	{
		for (size_t i = 0; i < COUNTOF(keySettings); ++i)
		{
			// Not `keyName`: helptext.h (included for DESTRUCT_MODES) owns a global of that name.
			const char *scancodeName;
			if (config_get_string_option(section, keySettingNames[i], &scancodeName))
			{
				SDL_Scancode scancode = SDL_GetScancodeFromName(scancodeName);
				if (scancode != SDL_SCANCODE_UNKNOWN)
					keySettings[i] = scancode;
			}
		}
	}

	section = config_find_section(config, "mouse", NULL);
	if (section != NULL)
	{
		for (size_t i = 0; i < COUNTOF(mouseSettings); ++i)
		{
			const char *mouseValue;
			if (config_get_string_option(section, mouseSettingNames[i], &mouseValue))
			{
				for (size_t val = 1; val <= COUNTOF(mouseSettingValues); ++val)
				{
					if (strcmp(mouseValue, mouseSettingValues[val - 1]))
						continue;

					mouseSettings[i] = val;
					break;
				}
			}
		}
	}

	section = config_find_section(config, "enhancements", NULL);
	if (section != NULL)
	{
		config_get_int_option(section, "boss_bar_style", &bossBarStyle);
		config_get_int_option(section, "boss_bar_layout", &bossBarLayout);
		config_get_int_option(section, "boss_bar_two_mode", &bossBarTwoMode);

		int armor_alarm_enabled = armorAlarm ? 1 : 0;
		config_get_int_option(section, "armor_alarm", &armor_alarm_enabled);
		armorAlarm = (armor_alarm_enabled != 0);

		int link_sounds_enabled = linkSounds ? 1 : 0;
		config_get_int_option(section, "link_sounds", &link_sounds_enabled);
		linkSounds = (link_sounds_enabled != 0);

		int debug_mode_enabled = debugMode ? 1 : 0;
		config_get_int_option(section, "debug_mode", &debug_mode_enabled);
		debugMode = (debug_mode_enabled != 0);

		int hang_timeout = crashlog_get_hang_timeout();
		config_get_int_option(section, "hang_timeout", &hang_timeout);
		crashlog_set_hang_timeout(hang_timeout);  // clamps into range

		int net_log_enabled = crashlog_get_netlog_enabled() ? 1 : 0;
		config_get_int_option(section, "net_log", &net_log_enabled);
		crashlog_set_netlog_enabled(net_log_enabled != 0);

		int enemy_bars_enabled = enemyBars ? 1 : 0;
		config_get_int_option(section, "enemy_bars", &enemy_bars_enabled);
		enemyBars = (enemy_bars_enabled != 0);

		config_get_int_option(section, "enemy_bar_layout", &enemyBarLayout);
		config_get_int_option(section, "enemy_bar_position", &enemyBarPosition);
		config_get_int_option(section, "enemy_bar_opacity", &enemyBarOpacity);

		int smooth_motion_enabled = smoothMotion ? 1 : 0;
		config_get_int_option(section, "smooth_motion", &smooth_motion_enabled);
		smoothMotion = (smooth_motion_enabled != 0);

		int extra_sparks_enabled = extraSparks ? 1 : 0;
		config_get_int_option(section, "extra_sparks", &extra_sparks_enabled);
		extraSparks = (extra_sparks_enabled != 0);

		int extra_parallax_enabled = extraParallax ? 1 : 0;
		config_get_int_option(section, "extra_parallax", &extra_parallax_enabled);
		extraParallax = (extra_parallax_enabled != 0);

		int mirrored_layers_enabled = mirroredLayers ? 1 : 0;
		config_get_int_option(section, "mirrored_layers", &mirrored_layers_enabled);
		mirroredLayers = (mirrored_layers_enabled != 0);

		// Music device (OPL3 / FluidSynth / Native MIDI) + SoundFont path. The
		// MIDI devices only take effect in a WITH_MIDI build; otherwise init_audio()
		// falls back to OPL (see loudness.c).
		const char *music_device_name;
		if (config_get_string_option(section, "music_device", &music_device_name))
		{
			for (int i = 0; i < MUSIC_DEVICE_MAX; ++i)
			{
				if (strcmp(music_device_name, music_device_names[i]) == 0)
				{
					music_device = (MusicDevice)i;
					break;
				}
			}
		}

		const char *soundfont_name;
		if (config_get_string_option(section, "soundfont", &soundfont_name))
			SDL_strlcpy(soundfont, soundfont_name, sizeof(soundfont));

		// Multiplayer lobby: remembered so hosting or rejoining is Enter-Enter next time.
		// Only the joiner's target port is stored with the address (as "host:port"); the
		// listen port is separate because a machine can be host one session and joiner the next.
		{
			const char *name;
			if (config_get_string_option(section, "net_player_name", &name))
				network_set_player_name(name);

			const char *host;
			// A command-line game (--net) already named its target; the remembered lobby
			// host is a prefill for the join screen and must not clobber it.
			if (!isNetworkGame
			    && config_get_string_option(section, "net_last_host", &host) && host[0] != '\0')
			{
				free(network_opponent_host);
				network_opponent_host = malloc_die(strlen(host) + 1);
				strcpy(network_opponent_host, host);

				// Split the stored "host:port" back apart; a bad or missing port keeps the
				// default, but the suffix is always cut off so the host still resolves.
				char *const colon = strrchr(network_opponent_host, ':');
				if (colon != NULL && colon != network_opponent_host)
				{
					const int port = SDL_atoi(colon + 1);
					if (port > 0 && port < 49152)
						network_opponent_port = (Uint16)port;
					*colon = '\0';
				}
			}

			int net_port = network_listen_port;
			config_get_int_option(section, "net_listen_port", &net_port);
			if (net_port > 0 && net_port < 49152)
				network_listen_port = (Uint16)net_port;

			// Which player the host flies (2 is the Dragonwing); the joiner takes the other.
			int net_host_player = network_host_player;
			config_get_int_option(section, "net_host_player", &net_host_player);
			if (net_host_player == 1 || net_host_player == 2)
				network_host_player = net_host_player;

			// Two-player save slots this machine wrote as player two (save_slot_online_player).
			int net_save_player_two = saveSlotPlayerTwo;
			config_get_int_option(section, "net_save_player_two", &net_save_player_two);
			if (net_save_player_two >= 0 && net_save_player_two <= 0xffff)
				saveSlotPlayerTwo = (Uint16)net_save_player_two;

			// Session game speed forced on both players when hosting (1..5, 4 = Normal).
			int net_game_speed = network_host_game_speed;
			config_get_int_option(section, "net_host_game_speed", &net_game_speed);
			if (net_game_speed >= 1 && net_game_speed <= 5)
				network_host_game_speed = net_game_speed;

			// Which Destruct battle a Destruct session fights (0..4, the data-backed modes).
			int net_destruct_mode = network_host_destruct_mode;
			config_get_int_option(section, "net_host_destruct_mode", &net_destruct_mode);
			if (net_destruct_mode >= 0 && net_destruct_mode < DESTRUCT_MODES)
				network_host_destruct_mode = net_destruct_mode;

			// Tick-rate cap vs input lag; see the comment on network_delay. Exposed here so a
			// link can be tuned without a rebuild; the host's value is what both sides use.
			int net_delay = network_delay;
			config_get_int_option(section, "net_delay", &net_delay);
			if (net_delay >= 1 && net_delay <= 6)
				network_delay = net_delay;

			// Rollback netcode (local input applies instantly, peer predicted and
			// corrected by re-simulation) vs the original delay-based lockstep.
			// Host-authoritative like the rest of the sim settings.
			config_get_bool_option(section, "net_rollback", &net_rollback);

			// Repair a detected desync by streaming the host's state to the
			// joiner (net_rollback.h).  Host-authoritative; rollback sessions only.
			config_get_bool_option(section, "net_desync_recovery", &net_desync_recovery);

			// Online Campaign: pay every kill and score pickup to both players
			// (player.h).  Host-authoritative; Campaign sessions only.
			config_get_bool_option(section, "net_campaign_shared_credit", &coopSharedCredit);

			// Individual credit only: pay combat cash (pickups, kills, bounties) twice
			// (player.h). The key keeps its historical name; renaming would drop the
			// setting from existing configs. And whether an Endless kill feeds both
			// ships' combo streaks or only the shooter's.
			config_get_bool_option(section, "net_coop_double_pickups", &coopDoubleEarnings);
			config_get_bool_option(section, "net_endless_combo_shared", &network_host_endless_combo_shared);

			// The chart rule was a Same/Varied flag before the Shuffle rules joined it. Read the
			// old key first so a host that set it keeps their choice, then let the new one win.
			bool net_endless_base_same = network_host_endless_base_rule == (int)ENDLESS_BASE_SAME;
			if (config_get_bool_option(section, "net_endless_base_same", &net_endless_base_same))
				network_host_endless_base_rule = net_endless_base_same ? (int)ENDLESS_BASE_SAME
				                                                       : (int)ENDLESS_BASE_VARIED;
			int net_endless_base_rule = network_host_endless_base_rule;
			config_get_int_option(section, "net_endless_base_rule", &net_endless_base_rule);
			if (net_endless_base_rule >= 0 && net_endless_base_rule < ENDLESS_BASE_RULE_COUNT)
				network_host_endless_base_rule = net_endless_base_rule;

			// Online Arcade: the classic linked pair, or two Separate personal arcades.
			config_get_bool_option(section, "net_arcade_separate", &arcadeSeparateShips);

			// Arcade's third shape, and which of the three battles it races.
			config_get_bool_option(section, "net_arcade_timed_battle", &network_host_timed_battle);
			int net_battle_level = network_host_battle_level;
			config_get_int_option(section, "net_battle_level", &net_battle_level);
			if (net_battle_level >= 1 && net_battle_level <= NET_TIMED_BATTLE_LEVELS)
				network_host_battle_level = net_battle_level;

			// Single-player determinism harness: verify the rollback snapshot
			// every tick (see rollback.h).  Costs a second sim pass per tick.
			config_get_bool_option(section, "rollback_selftest", &rollback_selftest);
		}

		// Legacy keys from when only the Mega Pulse had these settings; the new per-weapon
		// keys below override them when present.
		config_get_int_option(section, "mega_pulse_sparks", &superSparkMode[SSW_MEGA_PULSE]);
		int legacy_cap = superSparkClassicCap[SSW_MEGA_PULSE] ? 1 : 0;
		config_get_int_option(section, "mega_pulse_classic_cap", &legacy_cap);
		superSparkClassicCap[SSW_MEGA_PULSE] = (legacy_cap != 0);

		for (int i = 0; i < SSW_COUNT; ++i)
		{
			config_get_int_option(section, superSparkKeys[i], &superSparkMode[i]);
			if (superSparkMode[i] < 0 || superSparkMode[i] >= SUPER_SPARKS_COUNT)
				superSparkMode[i] = SUPER_SPARKS_ON;

			int spark_cap = superSparkClassicCap[i] ? 1 : 0;
			config_get_int_option(section, superSparkCapKeys[i], &spark_cap);
			superSparkClassicCap[i] = (spark_cap != 0);
		}

		config_get_int_option(section, "superspark_wallop_second_bolt", &wallopSecondBolt);
		if (wallopSecondBolt < 0 || wallopSecondBolt >= SUPER_SPARKS_COUNT)
			wallopSecondBolt = SUPER_SPARKS_AUTO;

		for (int i = 0; i < EDW_COUNT; ++i)
		{
			config_get_int_option(section, epDiffKeys[i], &epDiffMode[i]);
			if (epDiffMode[i] < 0 || epDiffMode[i] >= EPDIFF_MODE_COUNT)
				epDiffMode[i] = EPDIFF_AUTO;
		}

		config_get_int_option(section, "gauge_grad_generator", &gaugeGradGenerator);
		config_get_int_option(section, "gauge_grad_shield", &gaugeGradShield);
		config_get_int_option(section, "gauge_grad_armor", &gaugeGradArmor);

		int gauge_flash_shield_enabled = gaugeFlashShield ? 1 : 0;
		config_get_int_option(section, "gauge_flash_shield", &gauge_flash_shield_enabled);
		gaugeFlashShield = (gauge_flash_shield_enabled != 0);

		int gauge_flash_armor_enabled = gaugeFlashArmor ? 1 : 0;
		config_get_int_option(section, "gauge_flash_armor", &gauge_flash_armor_enabled);
		gaugeFlashArmor = (gauge_flash_armor_enabled != 0);

		int special_screen_tint = specialScreenTint ? 1 : 0;
		config_get_int_option(section, "special_screen_tint", &special_screen_tint);
		specialScreenTint = (special_screen_tint != 0);

		config_get_int_option(section, "zica_l11_base", &zicaLaserBase);
		if (zicaLaserBase < 0 || zicaLaserBase >= ZICA_BASE_COUNT)
			zicaLaserBase = ZICA_BASE_EP4;

		config_get_int_option(section, "zica_l11_length", &zicaLaserLength);
		if (zicaLaserLength < 0 || zicaLaserLength >= ZICA_LEN_COUNT)
			zicaLaserLength = ZICA_LEN_SHORT;

		int zica_l11_lock = zicaLaserLock ? 1 : 0;
		config_get_int_option(section, "zica_l11_lock", &zica_l11_lock);
		zicaLaserLock = (zica_l11_lock != 0);

		// Back-compat: earlier builds saved this as 0/1 (bool) or 0-4 (mode); any non-zero = on.
		int zica_laser_buff = zicaLaserBuff ? 1 : 0;
		config_get_int_option(section, "zica_laser_buff", &zica_laser_buff);
		zicaLaserBuff = (zica_laser_buff != 0);

		int charge_laser_cannon = chargeLaserCannon ? 1 : 0;
		config_get_int_option(section, "charge_laser_cannon", &charge_laser_cannon);
		chargeLaserCannon = (charge_laser_cannon != 0);

		int restore_base_dispensers = restoreBaseDispensers ? 1 : 0;
		config_get_int_option(section, "restore_base_dispensers", &restore_base_dispensers);
		restoreBaseDispensers = (restore_base_dispensers != 0);

		int arcade_life_boost = arcadeLifeBoost ? 1 : 0;
		config_get_int_option(section, "arcade_life_boost", &arcade_life_boost);
		arcadeLifeBoost = (arcade_life_boost != 0);

		int arcade_random_balls = arcadeRandomBalls ? 1 : 0;
		config_get_int_option(section, "arcade_random_balls", &arcade_random_balls);
		arcadeRandomBalls = (arcade_random_balls != 0);

		int arcade_rear_gun_scale = arcadeRearGunScale ? 1 : 0;
		config_get_int_option(section, "arcade_rear_gun_scale", &arcade_rear_gun_scale);
		arcadeRearGunScale = (arcade_rear_gun_scale != 0);

		int unused_shop_sprites = unusedShopSprites ? 1 : 0;
		config_get_int_option(section, "unused_shop_sprites", &unused_shop_sprites);
		unusedShopSprites = (unused_shop_sprites != 0);

		int centered_shot_hitboxes = centeredShotHitboxes ? 1 : 0;
		config_get_int_option(section, "centered_shot_hitboxes", &centered_shot_hitboxes);
		centeredShotHitboxes = (centered_shot_hitboxes != 0);

		int guided_shot_screen_aim = guidedShotScreenAim ? 1 : 0;
		config_get_int_option(section, "guided_shot_screen_aim", &guided_shot_screen_aim);
		guidedShotScreenAim = (guided_shot_screen_aim != 0);

		config_get_int_option(section, "xmas", &xmasMode);
		if (xmasMode < -1 || xmasMode > 1)
			xmasMode = 0;

		// Clamp to valid ranges in case of a hand-edited or stale config.
		if (bossBarStyle < BOSS_BAR_CLASSIC || bossBarStyle > BOSS_BAR_ENHANCED)
			bossBarStyle = BOSS_BAR_ENHANCED;
		if (bossBarLayout < BOSS_BAR_TOP || bossBarLayout > BOSS_BAR_RIGHT)
			bossBarLayout = BOSS_BAR_TOP;
		if (bossBarTwoMode < BOSS_BAR_TWO_TOGETHER || bossBarTwoMode > BOSS_BAR_TWO_STACKED)
			bossBarTwoMode = BOSS_BAR_TWO_SPLIT;
		if (enemyBarLayout < ENEMY_BAR_HORIZONTAL || enemyBarLayout > ENEMY_BAR_VERTICAL)
			enemyBarLayout = ENEMY_BAR_HORIZONTAL;
		if (enemyBarPosition < ENEMY_BAR_POS_BOTTOM || enemyBarPosition > ENEMY_BAR_POS_CENTER)
			enemyBarPosition = ENEMY_BAR_POS_BOTTOM;
		if (enemyBarOpacity < 0)
			enemyBarOpacity = 0;
		else if (enemyBarOpacity > 100)
			enemyBarOpacity = 100;
		if (gaugeGradGenerator < 0 || gaugeGradGenerator >= GAUGE_GRAD_COUNT)
			gaugeGradGenerator = GAUGE_GRAD_UP;
		if (gaugeGradShield < 0 || gaugeGradShield >= GAUGE_GRAD_COUNT)
			gaugeGradShield = GAUGE_GRAD_RIGHT;
		if (gaugeGradArmor < 0 || gaugeGradArmor >= GAUGE_GRAD_COUNT)
			gaugeGradArmor = GAUGE_GRAD_LEFT;

		// The set the Custom preset hands back, kept only while it still fits the preset table.
		const char *customSet;
		int customSetShape = 0;
		config_get_int_option(section, "custom_set_shape", &customSetShape);
		if (config_get_string_option(section, "custom_set", &customSet)
		    && (unsigned int)customSetShape == enhancementTableShape())
		{
			enhancementLoadCustomSet(customSet);
		}

		for (int i = 0; i < expertSettingsCount; ++i)
			config_get_int_option(section, expertSettings[i].cfgKey, expertSettings[i].value);
		clamp_expert_settings();  // guard against a hand-edited or stale config

		// Custom Weapon Creator: master toggle + the saved per-power-level raw designs.
		// Each level is a compact comma-separated integer blob (see custom_weapon.c);
		// customWeaponInit() fills in a default design when none is present, and clamps.
		int custom_weapon_enabled = customWeaponEnabled ? 1 : 0;
		config_get_int_option(section, "custom_weapon_enabled", &custom_weapon_enabled);
		customWeaponEnabled = (custom_weapon_enabled != 0);

		// Weapon-wide identity (maps to the single port; shared across all levels).
		const char *custom_weapon_name;
		if (config_get_string_option(section, "custom_weapon_name", &custom_weapon_name))
		{
			strncpy(customWeaponName, custom_weapon_name, sizeof(customWeaponName) - 1);
			customWeaponName[sizeof(customWeaponName) - 1] = '\0';
		}
		config_get_int_option(section, "custom_weapon_cost",          &customWeaponCost);
		config_get_int_option(section, "custom_weapon_power_use",     &customWeaponPowerUse);
		config_get_int_option(section, "custom_weapon_equip_slot",    &customWeaponEquipSlot);
		config_get_int_option(section, "custom_weapon_item_graphic",  &customWeaponItemGraphic);
		config_get_int_option(section, "custom_weapon_charge_stages", &customWeaponChargeStages);
		config_get_int_option(section, "custom_weapon_modes",         &customWeaponModes);
		config_get_int_option(section, "custom_weapon_sk_mount",      &customSidekickMount);
		config_get_int_option(section, "custom_weapon_sk_sprite",     &customSidekickSprite);
		config_get_int_option(section, "custom_weapon_sk_frames",     &customSidekickFrames);
		config_get_int_option(section, "custom_weapon_sk_frame_step", &customSidekickFrameStep);
		config_get_int_option(section, "custom_weapon_sk_animate",    &customSidekickAnimate);

		// Each (mode, power level)'s raw weapon: custom_weapon_m<M>_l<N>_raw. Mode 0 also
		// accepts the pre-modes key custom_weapon_l<N>_raw so an older config migrates.
		for (int m = 0; m < CUSTOM_WEAPON_MODES; ++m)
			for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
			{
				char key[64];
				const char *blob;
				if (m == 0)
				{
					snprintf(key, sizeof(key), "custom_weapon_l%d_raw", p + 1);
					if (config_get_string_option(section, key, &blob))
						customWeaponDeserializeLevel(0, p, blob);
				}
				snprintf(key, sizeof(key), "custom_weapon_m%d_l%d_raw", m + 1, p + 1);
				if (config_get_string_option(section, key, &blob))
					customWeaponDeserializeLevel(m, p, blob);
			}

		customWeaponEditLevel = 0;
		customWeaponEditMode = 0;
	}

	// Store the complete Endless debug setup in its own section. endless_save.c owns
	// the format so config.c does not depend on perk or modifier details.
	endlessDebugConfigLoad(config_find_section(config, "endless_debug", NULL));

	// The endless all-time record (furthest zone ever reached), its own section because it is a
	// player record because it is written during a run rather than during config changes.
	endlessRecordConfigLoad(config_find_section(config, "endless", NULL));
	coopCampaignScoreConfigLoad(config_find_section(config, "coop_scores", NULL));

	// Smooth Motion owns the sub-pixel render path. Keep it disabled when motion
	// interpolation is off, then apply the scaler constraint to the final state.
	set_smooth_motion(smoothMotion);
	if (render_supersample != 1)
		scaler = scaler_plain_equivalent(scaler);

	fclose(file);

	return true;
}

bool save_opentyrian_config(void)
{
	Config *config = &opentyrian_config;
	
	ConfigSection *section;
	
	section = config_find_or_add_section(config, "video", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory
	
	config_set_int_option(section, "fullscreen", fullscreen_display);
	
	config_set_string_option(section, "scaler", scalers[scaler].name);
	
	config_set_string_option(section, "scaling_mode", scaling_mode_names[scaling_mode]);

	config_set_int_option(section, "fps", fps_cap);

	config_set_int_option(section, "vsync", output_vsync ? 1 : 0);

	config_set_int_option(section, "show_fps", show_fps ? 1 : 0);

	config_set_int_option(section, SHIP_SENS_CFG, ship_sensitivity);

	config_set_int_option(section, "render_supersample", render_supersample);

	config_set_int_option(section, "smoothie_full_res", smoothie_full_res ? 1 : 0);

	section = config_find_or_add_section(config, "keyboard", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory

	for (size_t i = 0; i < COUNTOF(keySettings); ++i)
	{
		// Not `keyName`: helptext.h (included for DESTRUCT_MODES) owns a global of that name.
		const char *scancodeName = SDL_GetScancodeName(keySettings[i]);
		if (scancodeName[0] == '\0')
			scancodeName = NULL;
		config_set_string_option(section, keySettingNames[i], scancodeName);
	}

	// Best-effort: the directory almost always exists already, and a genuine failure surfaces at the
	// fopen below rather than here. Consumed so it doesn't read as an overlooked return.
#ifndef TARGET_WIN32
	const int mkdir_result = mkdir(get_user_directory(), 0700);
#else
	const int mkdir_result = mkdir(get_user_directory());
#endif
	(void)mkdir_result;

	// Persist mouse settings omitted by the Tyrian 2000 save format.
	section = config_find_or_add_section(config, "mouse", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory
	
	for (size_t i = 0; i < COUNTOF(mouseSettings); ++i)
		config_set_string_option(section, mouseSettingNames[i], mouseSettingValues[mouseSettings[i] - 1]);

	section = config_find_or_add_section(config, "enhancements", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory

	config_set_int_option(section, "boss_bar_style", bossBarStyle);
	config_set_int_option(section, "boss_bar_layout", bossBarLayout);
	config_set_int_option(section, "boss_bar_two_mode", bossBarTwoMode);
	config_set_int_option(section, "armor_alarm", armorAlarm ? 1 : 0);
	config_set_int_option(section, "link_sounds", linkSounds ? 1 : 0);
	config_set_int_option(section, "debug_mode", debugMode ? 1 : 0);
	config_set_int_option(section, "hang_timeout", crashlog_get_hang_timeout());
	config_set_int_option(section, "net_log", crashlog_get_netlog_enabled() ? 1 : 0);
	config_set_int_option(section, "enemy_bars", enemyBars ? 1 : 0);
	config_set_int_option(section, "enemy_bar_layout", enemyBarLayout);
	config_set_int_option(section, "enemy_bar_position", enemyBarPosition);
	config_set_int_option(section, "enemy_bar_opacity", enemyBarOpacity);
	config_set_int_option(section, "smooth_motion", smoothMotion ? 1 : 0);
	config_set_int_option(section, "extra_sparks", extraSparks ? 1 : 0);
	config_set_int_option(section, "extra_parallax", extraParallax ? 1 : 0);
	config_set_int_option(section, "mirrored_layers", mirroredLayers ? 1 : 0);
	config_set_string_option(section, "music_device", music_device_names[music_device]);

	config_set_string_option(section, "net_player_name", network_player_name);
	// Written as "host:port" so the joiner's target port is remembered with the address.
	char net_last_host[80];
	if (network_opponent_host != NULL && network_opponent_host[0] != '\0')
		snprintf(net_last_host, sizeof(net_last_host), "%s:%u", network_opponent_host, (unsigned)network_opponent_port);
	else
		net_last_host[0] = '\0';
	config_set_string_option(section, "net_last_host", net_last_host);
	config_set_int_option(section, "net_listen_port", network_listen_port);
	config_set_int_option(section, "net_host_player", network_host_player);
	config_set_int_option(section, "net_save_player_two", saveSlotPlayerTwo);
	config_set_int_option(section, "net_host_game_speed", network_host_game_speed);
	config_set_int_option(section, "net_host_destruct_mode", network_host_destruct_mode);
	config_set_int_option(section, "net_delay", network_delay);
	config_set_bool_option(section, "net_rollback", net_rollback, OFF_ON);
	config_set_bool_option(section, "net_desync_recovery", net_desync_recovery, OFF_ON);
	config_set_bool_option(section, "net_campaign_shared_credit", coopSharedCredit, OFF_ON);
	config_set_bool_option(section, "net_coop_double_pickups", coopDoubleEarnings, OFF_ON);
	config_set_bool_option(section, "net_arcade_separate", arcadeSeparateShips, OFF_ON);
	config_set_bool_option(section, "net_arcade_timed_battle", network_host_timed_battle, OFF_ON);
	config_set_int_option(section, "net_battle_level", network_host_battle_level);
	config_set_bool_option(section, "net_endless_combo_shared", network_host_endless_combo_shared, OFF_ON);
	config_set_int_option(section, "net_endless_base_rule", network_host_endless_base_rule);
	config_set_bool_option(section, "rollback_selftest", rollback_selftest, OFF_ON);
	config_set_string_option(section, "soundfont", soundfont);
	for (int i = 0; i < SSW_COUNT; ++i)
	{
		config_set_int_option(section, superSparkKeys[i], superSparkMode[i]);
		config_set_int_option(section, superSparkCapKeys[i], superSparkClassicCap[i] ? 1 : 0);
	}
	config_set_int_option(section, "superspark_wallop_second_bolt", wallopSecondBolt);
	for (int i = 0; i < EDW_COUNT; ++i)
		config_set_int_option(section, epDiffKeys[i], epDiffMode[i]);
	config_set_int_option(section, "gauge_grad_generator", gaugeGradGenerator);
	config_set_int_option(section, "gauge_grad_shield", gaugeGradShield);
	config_set_int_option(section, "gauge_grad_armor", gaugeGradArmor);
	config_set_int_option(section, "gauge_flash_shield", gaugeFlashShield ? 1 : 0);
	config_set_int_option(section, "gauge_flash_armor", gaugeFlashArmor ? 1 : 0);
	config_set_int_option(section, "special_screen_tint", specialScreenTint ? 1 : 0);
	config_set_int_option(section, "zica_l11_base", zicaLaserBase);
	config_set_int_option(section, "zica_l11_length", zicaLaserLength);
	config_set_int_option(section, "zica_l11_lock", zicaLaserLock ? 1 : 0);
	config_set_int_option(section, "zica_laser_buff", zicaLaserBuff ? 1 : 0);
	config_set_int_option(section, "charge_laser_cannon", chargeLaserCannon ? 1 : 0);
	config_set_int_option(section, "restore_base_dispensers", restoreBaseDispensers ? 1 : 0);
	config_set_int_option(section, "arcade_life_boost", arcadeLifeBoost ? 1 : 0);
	config_set_int_option(section, "arcade_random_balls", arcadeRandomBalls ? 1 : 0);
	config_set_int_option(section, "arcade_rear_gun_scale", arcadeRearGunScale ? 1 : 0);
	config_set_int_option(section, "unused_shop_sprites", unusedShopSprites ? 1 : 0);
	config_set_int_option(section, "centered_shot_hitboxes", centeredShotHitboxes ? 1 : 0);
	config_set_int_option(section, "guided_shot_screen_aim", guidedShotScreenAim ? 1 : 0);
	enhancementSaveCustomSet(section);
	config_set_int_option(section, "xmas", xmasMode);

	config_set_int_option(section, "custom_weapon_enabled", customWeaponEnabled ? 1 : 0);
	// Weapon-wide identity (maps to the single port; shared across all levels).
	config_set_string_option(section, "custom_weapon_name",    customWeaponName);
	config_set_int_option(section, "custom_weapon_cost",          customWeaponCost);
	config_set_int_option(section, "custom_weapon_power_use",     customWeaponPowerUse);
	config_set_int_option(section, "custom_weapon_equip_slot",    customWeaponEquipSlot);
	config_set_int_option(section, "custom_weapon_item_graphic",  customWeaponItemGraphic);
	config_set_int_option(section, "custom_weapon_charge_stages", customWeaponChargeStages);
	config_set_int_option(section, "custom_weapon_modes",         customWeaponModes);
	config_set_int_option(section, "custom_weapon_sk_mount",      customSidekickMount);
	config_set_int_option(section, "custom_weapon_sk_sprite",     customSidekickSprite);
	config_set_int_option(section, "custom_weapon_sk_frames",     customSidekickFrames);
	config_set_int_option(section, "custom_weapon_sk_frame_step", customSidekickFrameStep);
	config_set_int_option(section, "custom_weapon_sk_animate",    customSidekickAnimate);
	// Each (mode, power level)'s raw weapon, as a compact comma-separated integer blob.
	for (int m = 0; m < CUSTOM_WEAPON_MODES; ++m)
		for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
		{
			char key[64];
			// Saving is single-threaded. Keep the widest raw-weapon blob off the
			// comparatively small console stacks and reuse it for every level.
			static char blob[16384];
			snprintf(key, sizeof(key), "custom_weapon_m%d_l%d_raw", m + 1, p + 1);
			customWeaponSerializeLevel(m, p, blob, sizeof(blob));
			config_set_string_option(section, key, blob);
		}

	for (int i = 0; i < expertSettingsCount; ++i)
		config_set_int_option(section, expertSettings[i].cfgKey, *expertSettings[i].value);

	// The Debug Mode endless-effects layer (see the matching load above). endlessDebugConfigSave
	// declines to write during an endless run, so the section is only ever the CAMPAIGN setup.
	section = config_find_or_add_section(config, "endless_debug", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory
	endlessDebugConfigSave(section);

	// The endless all-time record (furthest zone ever reached). Unlike the debug layer, this one is
	// written during a run too.
	section = config_find_or_add_section(config, "endless", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory
	endlessRecordConfigSave(section);

	// Online co-op Campaign's own board, beside the Endless records for the same reason.
	section = config_find_or_add_section(config, "coop_scores", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory
	coopCampaignScoreConfigSave(section);

	FILE *file = dir_fopen(get_user_directory(), "opentyrian.cfg", "w");
	if (file == NULL)
		return false;

	config_write(config, file);
	
#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE
	fsync(fileno(file));
#endif
	fclose(file);
	
	return true;
}

static void playeritems_to_pitems(JE_PItemsType pItems, PlayerItems *items, JE_byte initial_episode_num)
{
	pItems[0]  = items->weapon[FRONT_WEAPON].id;
	pItems[1]  = items->weapon[REAR_WEAPON].id;
	pItems[2]  = items->super_arcade_mode;
	pItems[3]  = items->sidekick[LEFT_SIDEKICK];
	pItems[4]  = items->sidekick[RIGHT_SIDEKICK];
	pItems[5]  = items->generator;
	pItems[6]  = items->sidekick_level;
	pItems[7]  = items->sidekick_series;
	pItems[8]  = initial_episode_num;
	pItems[9]  = items->shield;
	pItems[10] = items->special;
	pItems[11] = items->ship;
}

static void pitems_to_playeritems(PlayerItems *items, const JE_PItemsType pItems, JE_byte *initial_episode_num)
{
	items->weapon[FRONT_WEAPON].id  = pItems[0];
	items->weapon[REAR_WEAPON].id   = pItems[1];
	items->super_arcade_mode        = pItems[2];
	items->sidekick[LEFT_SIDEKICK]  = pItems[3];
	items->sidekick[RIGHT_SIDEKICK] = pItems[4];
	items->generator                = pItems[5];
	items->sidekick_level           = pItems[6];
	items->sidekick_series          = pItems[7];
	if (initial_episode_num != NULL)
		*initial_episode_num        = pItems[8];
	items->shield                   = pItems[9];
	items->special                  = pItems[10];
	items->ship                     = pItems[11];
}

void JE_saveGame(JE_byte slot, const char *name)
{
	// Hardcore forbids saving at the data level, not only in the menus that offer it: any
	// path that reaches here mid-run (a stale disconnect-save flag, a future menu) must
	// leave the slot exactly as it was.  endlessSaveSlot refuses under the same rule, so
	// the campaign record and its endless sidecar stay a pair.
	if (endlessMode && endlessHardcore())
		return;

	const Uint32 coop_save_tag = 0xc74f0000u;
	const Uint32 dual_arcade_save_tag = 0xc7a50000u;
	saveFiles[slot-1].initialDifficulty = initialDifficulty;
	saveFiles[slot-1].gameHasRepeated = gameHasRepeated;
	saveFiles[slot-1].level = saveLevel;
	
	if (superTyrian)
		player[0].items.super_arcade_mode = SA_SUPERTYRIAN;
	else if (superArcadeMode == SA_NONE && onePlayerAction)
		player[0].items.super_arcade_mode = SA_ARCADE;
	else
		player[0].items.super_arcade_mode = superArcadeMode;
	
	playeritems_to_pitems(saveFiles[slot-1].items, &player[0].items, initial_episode_num);
	
	if (twoPlayerMode)
		playeritems_to_pitems(saveFiles[slot-1].lastItems, &player[1].items, 0);
	else
		playeritems_to_pitems(saveFiles[slot-1].lastItems, &player[0].last_items, 0);
	
	saveFiles[slot-1].score  = player[0].cash;
	saveFiles[slot-1].score2 = player[1].cash;
	
	memcpy(&saveFiles[slot-1].levelName, &lastLevelName, sizeof(lastLevelName));
	saveFiles[slot-1].cubes  = lastCubeMax;

	if (strcmp(lastLevelName, "Completed") == 0)
	{
		temp = episodeNum - 1;
		if (temp < 1)
		{
			temp = EPISODE_AVAILABLE; /* JE: {Episodemax is 4 for completion purposes} */
		}
		saveFiles[slot-1].episode = temp;
	}
	else
	{
		saveFiles[slot-1].episode = episodeNum;
	}

	saveFiles[slot-1].difficulty = difficultyLevel;
	saveFiles[slot-1].secretHint = secretHint;
	saveFiles[slot - 1].input1 = inputDevice[0];
	saveFiles[slot - 1].input2 = inputDevice[1];

	saveFiles[slot - 1].autoFireSpecial = autoFireSpecial;
	saveFiles[slot - 1].chargeSidekickAutofire = chargeSidekickAutofire;
	saveFiles[slot - 1].difficultyAdjust = difficultyAdjust;
	saveFiles[slot - 1].cheatInfiniteSidekickAmmo = cheatInfiniteSidekickAmmo;
	saveFiles[slot - 1].cheatInfiniteShields = cheatInfiniteShields;
	saveFiles[slot - 1].cheatInfiniteArmor = cheatInfiniteArmor;
	saveFiles[slot - 1].expertMode = expertMode;

	strcpy(saveFiles[slot-1].name, name);

	// Remember the seat this machine was flying, and forget it when a local game overwrites the
	// slot; a resume reads it back so nobody changes player number across the save.
	save_slot_set_online_player(slot, (isNetworkGame && twoPlayerMode) ? thisPlayerNum : 0);

	for (uint port = 0; port < 2; ++port)
	{
		// if two-player, use first player's front and second player's rear weapon
		saveFiles[slot-1].power[port] = player[twoPlayerMode ? port : 0].items.weapon[port].power;
	}

	/* Dual-ship records pack the two missing weapon powers and both modes into highScore2. Distinct
	 * co-op and arcade tags keep the shared slot page type-safe. */
	saveFiles[slot - 1].highScore2 = 0;
	if (dual_ship_mode())
	{
		const Uint32 extra = (player[0].items.weapon[REAR_WEAPON].power & 0x0f)
		                   | ((player[1].items.weapon[FRONT_WEAPON].power & 0x0f) << 4)
		                   | ((player[0].weapon_mode & 0x0f) << 8)
		                   | ((player[1].weapon_mode & 0x0f) << 12);
		saveFiles[slot - 1].highScore2 =
			(JE_longint)((coop_mode_active() ? coop_save_tag : dual_arcade_save_tag) | extra);
	}

	JE_saveConfiguration();
}

void JE_loadGame(JE_byte slot)
{
	JE_loadGameRecord(&saveFiles[slot-1], (slot-1) > 10);
}

void JE_loadGameRecord(const JE_SaveFileType *rec, bool twoP)
{
	superTyrian = false;
	onePlayerAction = false;
	twoPlayerMode = false;
	coopCampaignMode = false;
	coopEndlessMode = false;
	extraGame = false;
	galagaMode = false;
	timedBattleMode = false;
	endlessMode = false;  // saves are never endless (high-scores-only mode); always load as a normal game

	initialDifficulty = rec->initialDifficulty;
	gameHasRepeated   = rec->gameHasRepeated;
	twoPlayerMode     = twoP;
	// The tag only says the record carries two full loadouts; which co-op lobby is flying it is
	// the session's own business, so the network start path assigns the pair after this returns.
	coopCampaignMode  = isNetworkGame && twoP && save_record_is_coop(rec);
	difficultyLevel   = rec->difficulty;

	pitems_to_playeritems(&player[0].items, rec->items, &initial_episode_num);

	superArcadeMode = player[0].items.super_arcade_mode;

	if (superArcadeMode == SA_SUPERTYRIAN)
		superTyrian = true;
	if (superArcadeMode != SA_NONE)
		onePlayerAction = true;
	if (superArcadeMode > SA_LASTSHIP)
		superArcadeMode = SA_NONE;

	if (twoPlayerMode)
	{
		onePlayerAction = false;

		pitems_to_playeritems(&player[1].items, rec->lastItems, NULL);
		if (coop_mode_active())
		{
			player[1].is_dragonwing = false;
			player[0].last_items = player[0].items;
			player[1].last_items = player[1].items;
		}
	}
	else
	{
		pitems_to_playeritems(&player[0].last_items, rec->lastItems, NULL);
	}

	/* Compatibility with old version */
	if (player[1].items.sidekick_level < 101)
	{
		player[1].items.sidekick_level = 101;
		player[1].items.sidekick_series = player[1].items.sidekick[LEFT_SIDEKICK];
	}

	player[0].cash = rec->score;
	player[1].cash = rec->score2;

	mainLevel   = rec->level;
	cubeMax     = rec->cubes;
	lastCubeMax = cubeMax;

	secretHint = rec->secretHint;
	inputDevice[0] = rec->input1;
	inputDevice[1] = rec->input2;

	autoFireSpecial = rec->autoFireSpecial;
	chargeSidekickAutofire = rec->chargeSidekickAutofire;
	difficultyAdjust = rec->difficultyAdjust;
	cheatInfiniteSidekickAmmo = rec->cheatInfiniteSidekickAmmo;
	cheatInfiniteShields = rec->cheatInfiniteShields;
	cheatInfiniteArmor = rec->cheatInfiniteArmor;
	expertMode = rec->expertMode;

	for (uint port = 0; port < 2; ++port)
	{
		// if two-player, use first player's front and second player's rear weapon
		player[twoPlayerMode ? port : 0].items.weapon[port].power = rec->power[port];
	}
	/* Keyed on what the record says it is, not on what this session is: a record written by a
	 * two-complete-ships session carries the other half of both loadouts, and it has to come
	 * back whichever lobby is reading it. */
	if (save_record_is_coop(rec) || save_record_is_dual_arcade(rec))
	{
		const Uint32 extra = (Uint32)rec->highScore2;
		player[0].items.weapon[REAR_WEAPON].power = extra & 0x0f;
		player[1].items.weapon[FRONT_WEAPON].power = (extra >> 4) & 0x0f;
		player[0].weapon_mode = (extra >> 8) & 0x0f;
		player[1].weapon_mode = (extra >> 12) & 0x0f;
		if (player[0].weapon_mode == 0) player[0].weapon_mode = 1;
		if (player[1].weapon_mode == 0) player[1].weapon_mode = 1;
	}

	/* Which weapon bay each ship counts lives on depends on the shape of the session, and this
	 * is the one path that installs a loadout without going through newGame(). A resumed
	 * Separate arcade left on newGame()'s linked binding would spend ship two's rear-gun power
	 * every time it died, and hand it the wrong life count on the way in. */
	for (uint p = 0; p < COUNTOF(player); ++p)
		player[p].lives = &player[p].items.weapon[player_lives_port(p)].power;

	int episode = rec->episode;

	memcpy(&levelName, &rec->levelName, sizeof(levelName));

	if (strcmp(levelName, "Completed") == 0)
	{
		if (episode == EPISODE_AVAILABLE)
			episode = 1;
		else if (episode < EPISODE_AVAILABLE)
			episode++;
		/* Increment episode.  Episode EPISODE_AVAILABLE goes to 1. */
	}

	JE_initEpisode(episode);
	saveLevel = mainLevel;
	memcpy(&lastLevelName, &levelName, sizeof(levelName));
}

void JE_initProcessorType(void)
{
	/* Hardware detection was removed; use stable defaults on every system. */

	wild = false;
	superWild = false;
	smoothScroll = true;
	explosionTransparent = true;
	filtrationAvail = false;
	background2 = true;
	displayScore = true;

	switch (processorType)
	{
		case 1: /* 386 */
			background2 = false;
			displayScore = false;
			explosionTransparent = false;
			break;
		case 2: /* 486 - Default */
			break;
		case 3: /* High Detail */
			smoothScroll = false;
			break;
		case 4: /* Pentium */
			wild = true;
			filtrationAvail = true;
			break;
		case 5: /* Nonstandard VGA */
			smoothScroll = false;
			break;
		case 6: /* SuperWild */
			wild = true;
			superWild = true;
			filtrationAvail = true;
			break;
	}

	switch (gameSpeed)
	{
		case 1:  /* Slug Mode */
			fastPlay = 3;
			break;
		case 2:  /* Slower */
			fastPlay = 4;
			break;
		case 3: /* Slow */
			fastPlay = 5;
			break;
		case 4: /* Normal */
			fastPlay = 0;
			break;
		case 5: /* Pentium Hyper */
			fastPlay = 1;
			break;
	}

}

void JE_setNewGameSpeed(void)
{
	pentiumMode = false;

	Uint16 speed;
	switch (fastPlay)
	{
	default:
		assert(false);
		// fall through
	case 0:  // Normal
		speed = 0x4300;
		smoothScroll = true;
		frameCountMax = 2;
		break;
	case 1:  // Pentium Hyper
		speed = 0x3000;
		smoothScroll = true;
		frameCountMax = 2;
		break;
	case 2:
		speed = 0x2000;
		smoothScroll = false;
		frameCountMax = 2;
		break;
	case 3:  // Slug mode
		speed = 0x5300;
		smoothScroll = true;
		frameCountMax = 4;
		break;
	case 4:  // Slower
		speed = 0x4300;
		smoothScroll = true;
		frameCountMax = 3;
		break;
	case 5:  // Slow
		speed = 0x4300;
		smoothScroll = true;
		frameCountMax = 2;
		pentiumMode = true;
		break;
	}

	setDelaySpeed(speed);
	setDelay(frameCountMax);
}

void JE_encryptSaveTemp(void)
{
	JE_SaveGameTemp s3;
	JE_word x;
	JE_byte y;

	memcpy(&s3, &saveTemp, sizeof(s3));

	y = 0;
	for (x = 0; x < SAVE_FILE_SIZE; x++)
	{
		y += s3[x];
	}
	saveTemp[SAVE_FILE_SIZE] = y;

	y = 0;
	for (x = 0; x < SAVE_FILE_SIZE; x++)
	{
		y -= s3[x];
	}
	saveTemp[SAVE_FILE_SIZE+1] = y;

	y = 1;
	for (x = 0; x < SAVE_FILE_SIZE; x++)
	{
		y = (y * s3[x]) + 1;
	}
	saveTemp[SAVE_FILE_SIZE+2] = y;

	y = 0;
	for (x = 0; x < SAVE_FILE_SIZE; x++)
	{
		y = y ^ s3[x];
	}
	saveTemp[SAVE_FILE_SIZE+3] = y;

	for (x = 0; x < SAVE_FILE_SIZE; x++)
	{
		saveTemp[x] = saveTemp[x] ^ cryptKey[(x+1) % 10];
		if (x > 0)
		{
			saveTemp[x] = saveTemp[x] ^ saveTemp[x - 1];
		}
	}
}

void JE_decryptSaveTemp(void)
{
	JE_boolean correct = true;
	JE_SaveGameTemp s2;
	int x;
	JE_byte y;

	/* Decrypt save game file */
	for (x = (SAVE_FILE_SIZE - 1); x >= 0; x--)
	{
		// (unsigned) only to make the index's non-negativity local; x is >= 0 by the loop condition.
		const unsigned int k = (unsigned)(x + 1) % 10;
		OT_ASSUME(k < 10);
		s2[x] = (JE_byte)saveTemp[x] ^ (JE_byte)(cryptKey[k]);
		if (x > 0)
		{
			s2[x] ^= (JE_byte)saveTemp[x - 1];
		}

	}

	/* Verify the save checksum. */
	y = 0;
	for (x = 0; x < SAVE_FILE_SIZE; x++)
	{
		y += s2[x];
	}
	if (saveTemp[SAVE_FILE_SIZE] != y)
	{
		correct = false;
		printf("Failed additive checksum: %d vs %d\n", saveTemp[SAVE_FILE_SIZE], y);
	}

	y = 0;
	for (x = 0; x < SAVE_FILE_SIZE; x++)
	{
		y -= s2[x];
	}
	if (saveTemp[SAVE_FILE_SIZE+1] != y)
	{
		correct = false;
		printf("Failed subtractive checksum: %d vs %d\n", saveTemp[SAVE_FILE_SIZE+1], y);
	}

	y = 1;
	for (x = 0; x < SAVE_FILE_SIZE; x++)
	{
		y = (y * s2[x]) + 1;
	}
	if (saveTemp[SAVE_FILE_SIZE+2] != y)
	{
		correct = false;
		printf("Failed multiplicative checksum: %d vs %d\n", saveTemp[SAVE_FILE_SIZE+2], y);
	}

	y = 0;
	for (x = 0; x < SAVE_FILE_SIZE; x++)
	{
		y = y ^ s2[x];
	}
	if (saveTemp[SAVE_FILE_SIZE+3] != y)
	{
		correct = false;
		printf("Failed XOR'd checksum: %d vs %d\n", saveTemp[SAVE_FILE_SIZE+3], y);
	}

	/* Barf and die if save file doesn't validate */
	if (!correct)
	{
		fprintf(stderr, "Error reading save file!\n");
		crashlog_report_fatal("FATAL (save file checksum mismatch)",
		                      "tyrian.sav failed its checksum validation (corrupt or truncated save)");
		exit(255);
	}

	/* Keep decrypted version plz */
	memcpy(&saveTemp, &s2, sizeof(s2));
}

const char *get_user_directory(void)
{
	static char user_dir[500] = "";
	
	if (strlen(user_dir) == 0)
	{
#if defined(__SWITCH__)
		// Fixed writable location on the SD card; switch_platform_init() creates it.
		strcpy(user_dir, SWITCH_USER_DIR);
#elif defined(__vita__)
		// Fixed writable location on the memory card; vita_platform_init() creates it.
		strcpy(user_dir, VITA_USER_DIR);
#elif !defined(TARGET_WIN32)
		char *xdg_config_home = getenv("XDG_CONFIG_HOME");
		if (xdg_config_home != NULL)
		{
			snprintf(user_dir, sizeof(user_dir), "%s/opentyrian2000", xdg_config_home);
		}
		else
		{
			char *home = getenv("HOME");
			if (home != NULL)
			{
				snprintf(user_dir, sizeof(user_dir), "%s/.config/opentyrian2000", home);
			}
			else
			{
				strcpy(user_dir, ".");
			}
		}
#else
		strcpy(user_dir, ".");
#endif
	}
	
	return user_dir;
}

// for compatibility
Uint8 joyButtonAssign[4] = {1, 4, 5, 5};
Uint8 inputDevice_ = 0, jConfigure = 0, midiPort = 1;
bool configuration_loaded = false;

void JE_loadConfiguration(void)
{
	FILE *fi;
	int z;
	JE_byte *p;
	int y;
	
	fi = dir_fopen_warn(get_user_directory(), "tyrian.cfg", "rb");
	if (fi && ftell_eof(fi) == 28)
	{
		background2 = 0;
		fread_bool_die(&background2, fi);
		fread_u8_die(&gameSpeed, 1, fi);
		
		fread_u8_die(&inputDevice_, 1, fi);
		fread_u8_die(&jConfigure, 1, fi);
		
		fread_u8_die(&versionNum, 1, fi);
		
		fread_u8_die(&processorType, 1, fi);
		fread_u8_die(&midiPort, 1, fi);
		fread_u8_die(&soundEffects, 1, fi);
		fread_u8_die(&gammaCorrection, 1, fi);
		fread_s8_die(&difficultyLevel, 1, fi);
		
		fread_u8_die(joyButtonAssign, 4, fi);
		
		fread_u16_die(&tyrMusicVolume, 1, fi);
		fread_u16_die(&fxVolume, 1, fi);
		
		fread_u8_die(inputDevice, 2, fi);

		fread_u8_die(dosKeySettings, 8, fi);
		
		fclose(fi);
	}
	else
	{
		printf("\nInvalid or missing TYRIAN.CFG! Continuing using defaults.\n\n");
		
		soundEffects = 1;
		memcpy(&dosKeySettings, &defaultDosKeySettings, sizeof(dosKeySettings));
		background2 = true;
		tyrMusicVolume = 191;
		fxVolume = 191;
		gammaCorrection = 0;
		processorType = 4;  // detail level "Pentium"
		gameSpeed = 4;
		versionNum = 3;
	}
	
	load_opentyrian_config();
	
	if (tyrMusicVolume > 255)
		tyrMusicVolume = 255;
	if (fxVolume > 255)
		fxVolume = 255;
	
	set_volume(tyrMusicVolume, fxVolume);
	
	fi = dir_fopen_warn(get_user_directory(), "tyrian.sav", "rb");
	if (fi)
	{

		fseek(fi, 0, SEEK_SET);
		fread_die(saveTemp, 1, sizeof(saveTemp), fi);
		JE_decryptSaveTemp();

		/* Load fields individually because the C structure no longer matches the legacy save layout. */

		p = saveTemp;
		for (z = 0; z < SAVE_FILES_NUM; z++)
		{
			memcpy(&saveFiles[z].encode, p, sizeof(JE_word)); p += 2;
			saveFiles[z].encode = SDL_SwapLE16(saveFiles[z].encode);
			
			memcpy(&saveFiles[z].level, p, sizeof(JE_word)); p += 2;
			saveFiles[z].level = SDL_SwapLE16(saveFiles[z].level);
			
			memcpy(&saveFiles[z].items, p, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);
			
			memcpy(&saveFiles[z].score, p, sizeof(JE_longint)); p += 4;
			saveFiles[z].score = SDL_SwapLE32(saveFiles[z].score);
			
			memcpy(&saveFiles[z].score2, p, sizeof(JE_longint)); p += 4;
			saveFiles[z].score2 = SDL_SwapLE32(saveFiles[z].score2);
			
			/* SYN: Pascal strings are prefixed by a byte holding the length! */
			memset(&saveFiles[z].levelName, 0, sizeof(saveFiles[z].levelName));
			memcpy(&saveFiles[z].levelName, &p[1], *p);
			p += 10;
			
			/* This was a BYTE array, not a STRING, in the original. Go fig. */
			memcpy(&saveFiles[z].name, p, 14);
			p += 14;
			
			memcpy(&saveFiles[z].cubes, p, sizeof(JE_byte)); p++;
			memcpy(&saveFiles[z].power, p, sizeof(JE_byte) * 2); p += 2;
			memcpy(&saveFiles[z].episode, p, sizeof(JE_byte)); p++;
			memcpy(&saveFiles[z].lastItems, p, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);
			memcpy(&saveFiles[z].difficulty, p, sizeof(JE_byte)); p++;
			memcpy(&saveFiles[z].secretHint, p, sizeof(JE_byte)); p++;
			memcpy(&saveFiles[z].input1, p, sizeof(JE_byte)); p++;
			memcpy(&saveFiles[z].input2, p, sizeof(JE_byte)); p++;
			
			/* booleans were 1 byte in pascal; working around it */
			Uint8 temp;
			memcpy(&temp, p, 1); p++;
			saveFiles[z].gameHasRepeated = temp != 0;
			
			memcpy(&saveFiles[z].initialDifficulty, p, sizeof(JE_byte)); p++;
			
			memcpy(&saveFiles[z].highScore1, p, sizeof(JE_longint)); p += 4;
			saveFiles[z].highScore1 = SDL_SwapLE32(saveFiles[z].highScore1);
			
			memcpy(&saveFiles[z].highScore2, p, sizeof(JE_longint)); p += 4;
			saveFiles[z].highScore2 = SDL_SwapLE32(saveFiles[z].highScore2);
			
			memset(&saveFiles[z].highScoreName, 0, sizeof(saveFiles[z].highScoreName));
			memcpy(&saveFiles[z].highScoreName, &p[1], *p);
			p += 30;
			
			memcpy(&saveFiles[z].highScoreDiff, p, sizeof(JE_byte)); p++;

			memcpy(&temp, p, 1); p++;  // autoFireSpecial
			saveFiles[z].autoFireSpecial = temp != 0;

			memcpy(&temp, p, 1); p++;  // chargeSidekickAutofire
			saveFiles[z].chargeSidekickAutofire = temp;  // 0=OFF, 1=ON, 2=fully-charged-only, 3=ON (fastest)

			memcpy(&temp, p, 1); p++;  // difficultyAdjust
			saveFiles[z].difficultyAdjust = temp != 0;

			memcpy(&temp, p, 1); p++;  // cheatInfiniteSidekickAmmo
			saveFiles[z].cheatInfiniteSidekickAmmo = temp != 0;

			memcpy(&temp, p, 1); p++;  // cheatInfiniteShields
			saveFiles[z].cheatInfiniteShields = temp != 0;

			memcpy(&temp, p, 1); p++;  // cheatInfiniteArmor
			saveFiles[z].cheatInfiniteArmor = temp != 0;

			memcpy(&temp, p, 1); p++;  // expertMode
			saveFiles[z].expertMode = temp != 0;
		}

		/* The legacy save layout stores each score component as one byte. */
		editorLevel = (saveTemp[SIZEOF_SAVEGAMETEMP - 5] << 8) | saveTemp[SIZEOF_SAVEGAMETEMP - 6];

		// T2K High Scores are unencrypted after saveTemp
		for (z = 0; z < 10; ++z)
		{
			JE_byte len;

			for (y = 0; y < 3; ++y)
			{
				fread_s32_die(&t2kHighScores[z][y].score, 1, fi);
				t2kHighScores[z][y].score = SDL_SwapLE32(t2kHighScores[z][y].score);

				fread_u8_die(&len, 1, fi);
				fread_die(t2kHighScores[z][y].playerName, 1, 29, fi);

				t2kHighScores[z][y].playerName[len] = '\0';
				fread_u8_die(&t2kHighScores[z][y].difficulty, 1, fi);
			}
		}
		for (z = 10; z < 20; ++z)
		{
			JE_byte len;

			for (y = 0; y < 3; ++y)
			{
				fread_s32_die(&t2kHighScores[z][y].score, 1, fi);
				t2kHighScores[z][y].score = SDL_SwapLE32(t2kHighScores[z][y].score);

				fseek(fi, 4, SEEK_CUR); // Unknown long int that seems to have no effect
				fread_u8_die(&len, 1, fi);

				fread_die(t2kHighScores[z][y].playerName, 1, 29, fi);
				t2kHighScores[z][y].playerName[len] = '\0';
				fread_u8_die(&t2kHighScores[z][y].difficulty, 1, fi);
			}
		}

		fclose(fi);
	}
	else
	{
		/* We didn't have a save file! Let's make up random stuff! */
		editorLevel = 800;

		for (z = 0; z < 100; z++)
		{
			saveTemp[SAVE_FILES_SIZE + z] = initialItemAvail[z];
		}

		for (z = 0; z < SAVE_FILES_NUM; z++)
		{
			saveFiles[z].level = 0;

			for (y = 0; y < 14; y++)
			{
				saveFiles[z].name[y] = ' ';
			}
			saveFiles[z].name[14] = 0;

			saveFiles[z].highScore1 = ((mt_rand() % 20) + 1) * 1000;

			if (z % 6 > 2)
			{
				saveFiles[z].highScore2 = ((mt_rand() % 20) + 1) * 1000;
				strcpy(saveFiles[z].highScoreName, defaultTeamNames[mt_rand() % COUNTOF(defaultTeamNames)]);
			}
			else
			{
				strcpy(saveFiles[z].highScoreName, defaultHighScoreNames[mt_rand() % COUNTOF(defaultHighScoreNames)]);
			}

			saveFiles[z].autoFireSpecial = false;

			saveFiles[z].chargeSidekickAutofire = CHARGE_AUTOFIRE_ON;
			saveFiles[z].difficultyAdjust = true;
			saveFiles[z].cheatInfiniteSidekickAmmo = false;
			saveFiles[z].cheatInfiniteShields = false;
			saveFiles[z].cheatInfiniteArmor = false;
			saveFiles[z].expertMode = false;
		}

		for (z = 0; z < 10; ++z)
		{
			for (y = 0; y < 3; ++y)
			{
				// Timed Battle scores
				t2kHighScores[z][y].score = ((mt_rand() % 50) + 1) * 100;
				strcpy(t2kHighScores[z][y].playerName, defaultHighScoreNames[mt_rand() % COUNTOF(defaultHighScoreNames)]);
			}
		}
		for (z = 10; z < 20; ++z)
		{
			for (y = 0; y < 3; ++y)
			{
				// Main Game scores
				t2kHighScores[z][y].score = ((mt_rand() % 20) + 1) * 1000;
				if (z & 1)
					strcpy(t2kHighScores[z][y].playerName, defaultTeamNames[mt_rand() % COUNTOF(defaultTeamNames)]);
				else
					strcpy(t2kHighScores[z][y].playerName, defaultHighScoreNames[mt_rand() % COUNTOF(defaultHighScoreNames)]);
			}
		}
	}
	
	JE_initProcessorType();
	configuration_loaded = true;
}

void JE_saveConfiguration(void)
{
	FILE *f;
	JE_byte *p;
	int z;

	// Don't save nothing
	if (!configuration_loaded)
		return;

	p = saveTemp;
	for (z = 0; z < SAVE_FILES_NUM; z++)
	{
		JE_SaveFileType tempSaveFile;
		memcpy(&tempSaveFile, &saveFiles[z], sizeof(tempSaveFile));
		
		tempSaveFile.encode = SDL_SwapLE16(tempSaveFile.encode);
		memcpy(p, &tempSaveFile.encode, sizeof(JE_word)); p += 2;
		
		tempSaveFile.level = SDL_SwapLE16(tempSaveFile.level);
		memcpy(p, &tempSaveFile.level, sizeof(JE_word)); p += 2;
		
		memcpy(p, &tempSaveFile.items, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);
		
		tempSaveFile.score = SDL_SwapLE32(tempSaveFile.score);
		memcpy(p, &tempSaveFile.score, sizeof(JE_longint)); p += 4;
		
		tempSaveFile.score2 = SDL_SwapLE32(tempSaveFile.score2);
		memcpy(p, &tempSaveFile.score2, sizeof(JE_longint)); p += 4;
		
		/* SYN: Pascal strings are prefixed by a byte holding the length! */
		memset(p, 0, sizeof(tempSaveFile.levelName));
		*p = strlen(tempSaveFile.levelName);
		memcpy(&p[1], &tempSaveFile.levelName, *p);
		p += 10;
		
		/* This was a BYTE array, not a STRING, in the original. Go fig. */
		memcpy(p, &tempSaveFile.name, 14);
		p += 14;
		
		memcpy(p, &tempSaveFile.cubes, sizeof(JE_byte)); p++;
		memcpy(p, &tempSaveFile.power, sizeof(JE_byte) * 2); p += 2;
		memcpy(p, &tempSaveFile.episode, sizeof(JE_byte)); p++;
		memcpy(p, &tempSaveFile.lastItems, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);
		memcpy(p, &tempSaveFile.difficulty, sizeof(JE_byte)); p++;
		memcpy(p, &tempSaveFile.secretHint, sizeof(JE_byte)); p++;
		memcpy(p, &tempSaveFile.input1, sizeof(JE_byte)); p++;
		memcpy(p, &tempSaveFile.input2, sizeof(JE_byte)); p++;
		
		/* booleans were 1 byte in pascal; working around it */
		Uint8 temp = tempSaveFile.gameHasRepeated != false;
		memcpy(p, &temp, 1); p++;
		
		memcpy(p, &tempSaveFile.initialDifficulty, sizeof(JE_byte)); p++;
		
		tempSaveFile.highScore1 = SDL_SwapLE32(tempSaveFile.highScore1);
		memcpy(p, &tempSaveFile.highScore1, sizeof(JE_longint)); p += 4;
		
		tempSaveFile.highScore2 = SDL_SwapLE32(tempSaveFile.highScore2);
		memcpy(p, &tempSaveFile.highScore2, sizeof(JE_longint)); p += 4;
		
		memset(p, 0, sizeof(tempSaveFile.highScoreName));
		*p = strlen(tempSaveFile.highScoreName);
		memcpy(&p[1], &tempSaveFile.highScoreName, *p);
		p += 30;
		
		memcpy(p, &tempSaveFile.highScoreDiff, sizeof(JE_byte)); p++;

		temp = tempSaveFile.autoFireSpecial != false;
		memcpy(p, &temp, 1); p++;

		temp = tempSaveFile.chargeSidekickAutofire;  // 0=OFF, 1=ON, 2=fully-charged-only, 3=ON (fastest)
		memcpy(p, &temp, 1); p++;

		temp = tempSaveFile.difficultyAdjust != false;
		memcpy(p, &temp, 1); p++;

		temp = tempSaveFile.cheatInfiniteSidekickAmmo != false;
		memcpy(p, &temp, 1); p++;

		temp = tempSaveFile.cheatInfiniteShields != false;
		memcpy(p, &temp, 1); p++;

		temp = tempSaveFile.cheatInfiniteArmor != false;
		memcpy(p, &temp, 1); p++;

		temp = tempSaveFile.expertMode != false;
		memcpy(p, &temp, 1); p++;
	}
	
	saveTemp[SIZEOF_SAVEGAMETEMP - 6] = editorLevel >> 8;
	saveTemp[SIZEOF_SAVEGAMETEMP - 5] = editorLevel;
	
	JE_encryptSaveTemp();
	
	// Best-effort, as above: dir_fopen_warn is what actually reports a broken save directory.
#ifndef TARGET_WIN32
	const int mkdir_result = mkdir(get_user_directory(), 0700);
#else
	const int mkdir_result = mkdir(get_user_directory());
#endif
	(void)mkdir_result;

	f = dir_fopen_warn(get_user_directory(), "tyrian.sav", "wb");
	if (f != NULL)
	{
		fwrite_die(saveTemp, 1, sizeof(saveTemp), f);

		// T2K High Scores are unencrypted after saveTemp
		for (z = 0; z < 10; ++z)
		{
			JE_longint templi;
			JE_byte len;

			for (y = 0; y < 3; ++y)
			{
				templi = SDL_SwapLE32(t2kHighScores[z][y].score);
				len = strlen(t2kHighScores[z][y].playerName);
				fwrite_s32_die(&templi, f);

				fwrite_u8_die(&len, 1, f);
				fwrite_die(t2kHighScores[z][y].playerName, 1, 29, f);
				fwrite_u8_die(&t2kHighScores[z][y].difficulty, 1, f);
			}
		}
		for (z = 10; z < 20; ++z)
		{
			JE_longint templi;
			JE_byte len;

			for (y = 0; y < 3; ++y)
			{
				templi = SDL_SwapLE32(t2kHighScores[z][y].score);
				len = strlen(t2kHighScores[z][y].playerName);
				fwrite_s32_die(&templi, f);

				templi = 0x12345678;
				fwrite_s32_die(&templi, f); // Unknown long int that seems to have no effect

				fwrite_u8_die(&len, 1, f);
				fwrite_die(t2kHighScores[z][y].playerName, 1, 29, f);
				fwrite_u8_die(&t2kHighScores[z][y].difficulty, 1, f);
			}
		}

#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE
		fsync(fileno(f));
#endif
		fclose(f);
	}
	
	JE_decryptSaveTemp();
	
	f = dir_fopen_warn(get_user_directory(), "tyrian.cfg", "wb");
	if (f != NULL)
	{
		fwrite_bool_die(&background2, f);
		fwrite_u8_die(&gameSpeed, 1, f);
		
		fwrite_u8_die(&inputDevice_, 1, f);
		fwrite_u8_die(&jConfigure, 1, f);
		
		fwrite_u8_die(&versionNum, 1, f);
		fwrite_u8_die(&processorType, 1, f);
		fwrite_u8_die(&midiPort, 1, f);
		fwrite_u8_die(&soundEffects, 1, f);
		fwrite_u8_die(&gammaCorrection, 1, f);
		fwrite_s8_die(&difficultyLevel, 1, f);
		fwrite_u8_die(joyButtonAssign, 4, f);
		
		fwrite_u16_die(&tyrMusicVolume, f);
		fwrite_u16_die(&fxVolume, f);
		
		fwrite_u8_die(inputDevice, 2, f);
		
		fwrite_u8_die(dosKeySettings, 8, f);
		
#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE
		fsync(fileno(f));
#endif
		fclose(f);
	}

	save_opentyrian_config();
}

/* Packed save record for the network resume handshake: the host serializes the chosen 2-player
 * save with this and the joiner applies it through JE_loadGameRecord, so both machines start the
 * resumed session from byte-identical state. */

bool save_record_is_coop(const JE_SaveFileType *rec)
{
	return ((Uint32)rec->highScore2 & 0xffff0000u) == 0xc74f0000u;
}

/* The arcade half of the same marker: two complete ships, but arcade rules and two scores rather
 * than a shared campaign purse. Deliberately NOT save_record_is_coop, which is what admits a
 * record to the Campaign and Endless lobbies. */
bool save_record_is_dual_arcade(const JE_SaveFileType *rec)
{
	return ((Uint32)rec->highScore2 & 0xffff0000u) == 0xc7a50000u;
}

/* Which ruleset each of the record's two ships was flying, read straight out of the loadout
 * blocks (pItems[2], the super_arcade_mode byte). Ship two only means anything on a record that
 * carries two of them; a one-ship record's second block is player one's `last_items`. */
uint save_record_sa_ship(const JE_SaveFileType *rec, uint p)
{
	return p == 0 ? rec->items[2] : rec->lastItems[2];
}

void save_record_pack(Uint8 *buf, const JE_SaveFileType *rec)
{
	Uint8 *p = buf;

	Uint16 u16 = SDL_SwapLE16(rec->level);
	memcpy(p, &u16, 2); p += 2;

	memcpy(p, rec->items, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);
	memcpy(p, rec->lastItems, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);

	Uint32 u32 = SDL_SwapLE32((Uint32)rec->score);
	memcpy(p, &u32, 4); p += 4;
	u32 = SDL_SwapLE32((Uint32)rec->score2);
	memcpy(p, &u32, 4); p += 4;
	u32 = SDL_SwapLE32((Uint32)rec->highScore2);
	memcpy(p, &u32, 4); p += 4;

	memcpy(p, rec->levelName, sizeof(rec->levelName)); p += sizeof(rec->levelName);
	memcpy(p, rec->name, sizeof(rec->name)); p += sizeof(rec->name);

	*p++ = rec->cubes;
	*p++ = rec->power[0];
	*p++ = rec->power[1];
	*p++ = rec->episode;
	*p++ = rec->difficulty;
	*p++ = rec->secretHint;
	*p++ = rec->input1;
	*p++ = rec->input2;
	*p++ = rec->gameHasRepeated != false;
	*p++ = rec->initialDifficulty;
	*p++ = rec->autoFireSpecial != false;
	*p++ = rec->chargeSidekickAutofire;
	*p++ = rec->difficultyAdjust != false;
	*p++ = rec->cheatInfiniteSidekickAmmo != false;
	*p++ = rec->cheatInfiniteShields != false;
	*p++ = rec->cheatInfiniteArmor != false;
	*p++ = rec->expertMode != false;

	assert(p - buf == SAVE_RECORD_PACKED_SIZE);
}

void save_record_unpack(JE_SaveFileType *rec, const Uint8 *buf)
{
	const Uint8 *p = buf;

	memset(rec, 0, sizeof(*rec));

	Uint16 u16;
	memcpy(&u16, p, 2); p += 2;
	rec->level = SDL_SwapLE16(u16);

	memcpy(rec->items, p, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);
	memcpy(rec->lastItems, p, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);

	Uint32 u32;
	memcpy(&u32, p, 4); p += 4;
	rec->score = (JE_longint)SDL_SwapLE32(u32);
	memcpy(&u32, p, 4); p += 4;
	rec->score2 = (JE_longint)SDL_SwapLE32(u32);
	memcpy(&u32, p, 4); p += 4;
	rec->highScore2 = (JE_longint)SDL_SwapLE32(u32);

	memcpy(rec->levelName, p, sizeof(rec->levelName)); p += sizeof(rec->levelName);
	rec->levelName[sizeof(rec->levelName) - 1] = '\0';
	memcpy(rec->name, p, sizeof(rec->name)); p += sizeof(rec->name);
	rec->name[sizeof(rec->name) - 1] = '\0';

	rec->cubes = *p++;
	rec->power[0] = *p++;
	rec->power[1] = *p++;
	rec->episode = *p++;
	rec->difficulty = *p++;
	rec->secretHint = *p++;
	rec->input1 = *p++;
	rec->input2 = *p++;
	rec->gameHasRepeated = *p++ != 0;
	rec->initialDifficulty = *p++;
	rec->autoFireSpecial = *p++ != 0;
	rec->chargeSidekickAutofire = *p++;
	rec->difficultyAdjust = *p++ != 0;
	rec->cheatInfiniteSidekickAmmo = *p++ != 0;
	rec->cheatInfiniteShields = *p++ != 0;
	rec->cheatInfiniteArmor = *p++ != 0;
	rec->expertMode = *p++ != 0;

	assert(p - buf == SAVE_RECORD_PACKED_SIZE);
}
