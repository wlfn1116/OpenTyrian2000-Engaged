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
#include "config.h"

#include "audio.h"
#include "crashlog.h"
#include "custom_weapon.h"
#include "debug_flags.h"
#include "endless.h"       // endlessDebugConfigLoad/Save ([endless_debug] section)
#include "custom_episode.h"
#include "episodes.h"
#include "file.h"
#include "game_state.h"
#include "helptext.h"      // DESTRUCT_MODES bounds the persisted battle mode
#include "joystick.h"
#include "keyboard.h"
#include "net_rollback.h"
#include "network.h"
#include "opentyr.h"
#include "params.h"        // constantPlay bars the co-op Campaign board
#include "player.h"
#include "rollback.h"
#include "savegame.h"
#include "sfx.h"
#include "timing.h"
#include "video.h"
#include "video_scale.h"
#include "weapon_editor.h"

#include <assert.h>
#include <stdio.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

/* Configuration Load/Save handler */

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
           background2, smoothScroll, wild, superWild, starActive,
           topEnemyOver,
           skyEnemyOverAll,
           background2notTransparent;
static JE_boolean displayScore;

JE_byte    fastPlay;
JE_boolean pentiumMode;

JE_byte    gameSpeed;
JE_byte    processorType;  /* detail level: 1=Low 2=Normal 3=High 4=Pentium 5=Laptop VGA 6=Wild */

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

/* Per episode: "score|difficulty|names". Missing entries remain empty. */
static void coopCampaignScoreConfigSave(ConfigSection *section)
{
	if (section == NULL)
		return;

	for (int e = 0; e < COOP_CAMPAIGN_SCORE_EPISODES; ++e)
	{
		char key[32], line[64];
		snprintf(key, sizeof(key), "coop_campaign_%d", e + 1);
		snprintf(line, sizeof(line), "%lld|%u|%s", (long long)coopCampaignScores[e].score,
		         (unsigned)coopCampaignScores[e].difficulty, coopCampaignScores[e].name);
		config_set_string_option(section, key, line);

		snprintf(key, sizeof(key), "coop_campaign_credit_%d", e + 1);
		config_set_int_option(section, key, coopCampaignScores[e].credit);
	}
}

static void coopCampaignScoreConfigLoad(const ConfigSection *section)
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

		const long long score = strtoll(line, NULL, 10);
		coopCampaignScores[e].score = (score > 0) ? cash_clamp((Sint64)score) : 0;

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

/* Record a completed co-op Campaign episode under both lobby names. Eligibility stays here; see
 * doc/notes.md#online-saves. */
void coopCampaignScoreNote(void)
{
	const int e = initial_episode_num - 1;
	if (!coopCampaignMode || constantPlay || e < 0 || e >= COOP_CAMPAIGN_SCORE_EPISODES)
		return;

	// The base episode does not own a custom run's co-op score.
	if (customEpisodeActive())
		return;

	// Only the episode the run started in, played once: a later episode or a repeat carries cash
	// earned before it into this episode's record.
	if (episodeNum != initial_episode_num || gameHasRepeated)
		return;

	const Sint64 total = (Sint64)player[0].cash + (Sint64)player[1].cash;
	if (total <= coopCampaignScores[e].score)
		return;

	coopCampaignScores[e].score = cash_clamp(total);
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

/* Enhancement settings (persisted in the [enhancements] config section). */
int bossBarStyle   = BOSS_BAR_ENHANCED;
int bossBarLayout  = BOSS_BAR_TOP;
int bossBarTwoMode = BOSS_BAR_TWO_SPLIT;
int vulnerableCue  = VULN_CUE_BOSSES;
bool armorAlarm    = true;  // low-armor WARNING siren (Setup > Sound)
bool linkSounds    = true;  // 2P fuse/unfuse clink+spring (Setup > Sound)
// Disabling Debug Mode hides its menus and restores the stock layouts.
bool debugMode     = false;
/* Extends horizontal parallax to the full map span. Off preserves the stock amplitude. */
bool extraParallax = false;
/* Reflects columns beyond a map row's edge in both parallax modes. */
bool mirroredLayers = true;
/* On-screen sidekick fire buttons; see config.h. Drawn only where TOUCH_UI_BUTTONS is. */
bool touchSidekickButtons = true;
bool touchNavButtons = false;
/* On-screen button opacity; see config.h. */
int touchButtonOpacity = TOUCH_OPACITY_DEFAULT;
/* Thin health bar near an enemy once damaged (draw_enemy_health_bars in health_bars.c). */
bool enemyBars       = true;
int enemyBarLayout   = ENEMY_BAR_HORIZONTAL;
int enemyBarPosition = ENEMY_BAR_POS_BOTTOM;
int enemyBarOpacity  = 75;
// Interpolate the fixed 35 Hz simulation for high-refresh displays. Off renders
// one frame per simulation tick.
bool smoothMotion  = true;

void set_smooth_motion(bool enabled)
{
	// Preserve a saved supersampling choice during config load.
	if (enabled && !smoothMotion)
		render_supersample = 0;  // re-arm Auto supersampling
	smoothMotion = enabled;
	if (!smoothMotion)
		render_supersample = 1;
}

/* Bigger explosion "superspark" ring buffer (MAX_SUPERPIXELS) vs the classic 101-spark cap;
   off = the original sparser DOS spark showers. Read by JE_doSP (sparks.c). */
bool extraSparks = true;
/* Per-weapon superspark policy: shipped behavior, always on, or always off. */
int superSparkMode[SSW_COUNT] = { SUPER_SPARKS_ON, SUPER_SPARKS_ON, SUPER_SPARKS_ON, SUPER_SPARKS_ON };
/* Cap a weapon's projectile trail at the classic 101-spark limit even when extraSparks is
   on, so the trail keeps its classic density (JE_doSP calls in shots.c). On by default. */
bool superSparkClassicCap[SSW_COUNT] = { true, true, true, true };
/* Optional episode 4/5 Wallop Beam double-bolt pattern. */
int wallopSecondBolt = SUPER_SPARKS_ON;

/* Config keys for the per-weapon trail settings; indexed by SuperSparkWeapon. */
static const char *const superSparkKeys[SSW_COUNT]    = { "superspark_mega_pulse", "superspark_wallop_beam", "superspark_protron_b", "superspark_ice" };
static const char *const superSparkCapKeys[SSW_COUNT] = { "superspark_mega_pulse_cap", "superspark_wallop_beam_cap", "superspark_protron_b_cap", "superspark_ice_cap" };

/* Episode data selected for each non-spark difference item. Keep this in EpDiffWeapon order;
   the Engaged choices are listed in GUIDE.md under Episode Versions. */
int epDiffMode[EDW_COUNT] = {
	EPDIFF_EP13, EPDIFF_AUTO, EPDIFF_AUTO,                                // gameplay reworks
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

/* Look up a trail's classic cap from its base sprite. Animated frames and unknown sprites do
   not identify a capped weapon. */
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
/* Gradient direction for each HUD gauge; Up is the shipped style. */
int gaugeGradGenerator = GAUGE_GRAD_UP;
int gaugeGradShield    = GAUGE_GRAD_RIGHT;
int gaugeGradArmor     = GAUGE_GRAD_LEFT;
bool gaugeFlashShield  = true;
bool gaugeFlashArmor   = true;
/* Let a burning flare special grade the whole screen its colour, as shipped (the paint is in
   game_loop.c; the flare that installs it is JE_doSpecialShot in specials.c). */
bool specialScreenTint = true;
/* Zica Laser Lv11 pattern (JE_applyZicaLaserConfig in episodes.c). */
int zicaLaserBase = ZICA_BASE_EP4;      /* ZICA_BASE_*: Lv11 shot pattern */
bool reworkEnabled[REWORK_COUNT] = {
	[REWORK_ZICA_BUFF] = true, [REWORK_ATTRACTOR] = true, [REWORK_ORANGE_SHIELD] = true,
	[REWORK_RAMMING] = true, [REWORK_ITEM_NAMES] = true,
};
/* Re-add the cut DOS "Charge-Laser Cannon" sidekick to its original shops + the debug
   menu (JE_applyChargeLaserCannon in episodes.c). */
bool chargeLaserCannon = true;
/* Wake the dormant dispenser bases (enemy 80-83; JE_makeEnemy in game_loop.c). Campaign
   only; Endless ignores the toggle and asks the zone instead. */
bool restoreBaseDispensers = true;
/* Arcade modes only: a ship's shield and armour ceilings scale with its life count
   (arcade_life_scaling_active in player.c). Off leaves the vanilla hull numbers. */
bool arcadeLifeBoost = true;
/* Arcade modes only: every weapon ball a level drops is re-rolled inside its own class
   (JE_makeEnemy in game_loop.c). Off = the hand-placed pickups the level scripts specify. */
bool arcadeRandomBalls = true;
/* In solo Arcade, add spare lives to the rear gun's banked power. */
bool arcadeRearGunScale = true;
/* Give unused sheet icons to items that otherwise reuse an icon or have none. */
bool unusedShopSprites = true;
bool shipEditorStars = true;
/* Use sprite centres as projectile and target hit-test anchors. */
bool centeredShotHitboxes = true;
/* Steer a weapon-table guided shot toward the enemy's screen x (ex + mapoffset), where the
 * collision loop measures it (player_shot_aim_step in shots.c). */
bool guidedShotScreenAim = false;
/* Christmas mode override: -1 = auto-detect by date (original), 0 = force off, 1 = force
   on. Set to 0/1 by the Enhancements toggle so the choice persists. */
int xmasMode = 0;

/* Every setting the Enhancements menu edits, with the value each preset writes to it. */
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
	{ .intSetting = &vulnerableCue,  .vanilla = VULN_CUE_OFF, .engaged = VULN_CUE_BOSSES },

	/* Gauges. */
	{ .intSetting = &gaugeGradGenerator, .vanilla = GAUGE_GRAD_UP, .engaged = GAUGE_GRAD_UP },
	{ .intSetting = &gaugeGradShield,    .vanilla = GAUGE_GRAD_UP, .engaged = GAUGE_GRAD_RIGHT },
	{ .intSetting = &gaugeGradArmor,     .vanilla = GAUGE_GRAD_UP, .engaged = GAUGE_GRAD_LEFT },
	{ .boolSetting = &gaugeFlashShield,  .vanilla = false,         .engaged = true },
	{ .boolSetting = &gaugeFlashArmor,   .vanilla = false,         .engaged = true },

	/* Weapons. */
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

	/* Zica Laser Lv11, and the Endless reworks. */
	{ .intSetting = &zicaLaserBase,   .vanilla = ZICA_BASE_AUTO, .engaged = ZICA_BASE_EP4 },
	{ .boolSetting = &reworkEnabled[REWORK_ZICA_BUFF],     .vanilla = false, .engaged = true },
	{ .boolSetting = &reworkEnabled[REWORK_ATTRACTOR],     .vanilla = false, .engaged = true },
	{ .boolSetting = &reworkEnabled[REWORK_ORANGE_SHIELD], .vanilla = false, .engaged = true },
	{ .boolSetting = &reworkEnabled[REWORK_RAMMING],       .vanilla = false, .engaged = true },
	{ .boolSetting = &reworkEnabled[REWORK_ITEM_NAMES],    .vanilla = false, .engaged = true },

	/* Vanilla uses Auto throughout. Engaged pins the Xega Ball and presentation rows listed in
	 * GUIDE.md; the other gameplay changes stay on Auto. */
	{ .intSetting = &epDiffMode[EDW_XEGA_BALL],       .vanilla = EPDIFF_AUTO, .engaged = EPDIFF_EP13 },
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

/* Reject positional Custom presets when the settings table fingerprint changes. */
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

static void load_int_flag(const ConfigSection *section, const char *key, bool *flag)
{
	int value = *flag ? 1 : 0;
	config_get_int_option(section, key, &value);
	*flag = (value != 0);
}

static const char *const reworkKeys[] = {
	"zica_laser_buff", "rework_attractor", "rework_orange_shield", "rework_ramming",
	"rework_item_names"
};
COMPILE_TIME_ASSERT(rework_keys_cover_reworks, COUNTOF(reworkKeys) == REWORK_COUNT);

void reworkConfigLoad(const ConfigSection *section)
{
	for (int i = 0; i < REWORK_COUNT; ++i)
		load_int_flag(section, reworkKeys[i], &reworkEnabled[i]);
}

void reworkConfigSave(ConfigSection *section)
{
	for (int i = 0; i < REWORK_COUNT; ++i)
		config_set_int_option(section, reworkKeys[i], reworkEnabled[i] ? 1 : 0);
	config_remove_option(section, "zica_l11_length");
	config_remove_option(section, "zica_l11_lock");
}

static bool load_opentyrian_config(void)
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

		const char* scaling_mode;
		if (config_get_string_option(section, "scaling_mode", &scaling_mode))
			set_scaling_mode_by_name(scaling_mode);

		config_get_int_option(section, "fps", &fps_cap);
		set_fps(fps_cap);

		int vsync_enabled = output_vsync ? 1 : 0;
		config_get_int_option(section, "vsync", &vsync_enabled);
		set_vsync(vsync_enabled != 0);

		load_int_flag(section, "show_fps", &show_fps);

		config_get_int_option(section, "crt_filter", &crt_filter);
		if (crt_filter < CRT_FILTER_OFF || crt_filter >= CRT_FILTER_COUNT)
			crt_filter = CRT_FILTER_OFF;
		config_get_int_option(section, "crt_strength", &crt_strength);
		if (crt_strength < CRT_STRENGTH_LIGHT || crt_strength >= CRT_STRENGTH_COUNT)
			crt_strength = CRT_STRENGTH_MEDIUM;
		config_get_int_option(section, "crt_curvature", &crt_curvature);
		if (crt_curvature < CRT_CURVATURE_OFF || crt_curvature >= CRT_CURVATURE_COUNT)
			crt_curvature = CRT_CURVATURE_OFF;
		config_get_int_option(section, "crt_mask", &crt_mask);
		if (crt_mask < CRT_MASK_OFF || crt_mask >= CRT_MASK_COUNT)
			crt_mask = CRT_MASK_GRILLE;
		config_get_int_option(section, "crt_corners", &crt_corners);
		if (crt_corners < CRT_CORNERS_SQUARE || crt_corners >= CRT_CORNERS_COUNT)
			crt_corners = CRT_CORNERS_SQUARE;
		config_get_int_option(section, "crt_rolling_bar", &crt_rolling_bar);
		if (crt_rolling_bar < CRT_ROLLING_BAR_OFF || crt_rolling_bar >= CRT_ROLLING_BAR_COUNT)
			crt_rolling_bar = CRT_ROLLING_BAR_OFF;

		// Ship-control sensitivity slider: touch on the handheld ports, mouse on desktop.
		config_get_int_option(section, SHIP_SENS_CFG, &ship_sensitivity);
		if (ship_sensitivity < 0 || ship_sensitivity > SHIP_SENS_MAX)
			ship_sensitivity = SHIP_SENS_DEFAULT;

		// Sub-pixel supersampling: 0 = Auto, 1 = off, 2..5 fixed, 6 = Native (match the display).
		config_get_int_option(section, "render_supersample", &render_supersample);
		if (render_supersample < 0)
			render_supersample = 0;
		else if (render_supersample > RENDER_SUPERSAMPLE_NATIVE)
			render_supersample = RENDER_SUPERSAMPLE_NATIVE;

		load_int_flag(section, "smoothie_full_res", &smoothie_full_res);

		config_get_int_option(section, "render_threads", &render_threads);
		if (render_threads < 0)
			render_threads = 0;
		else if (render_threads > video_rows_max_threads())
			render_threads = video_rows_max_threads();
	}

	section = config_find_section(config, "keyboard", NULL);
	if (section != NULL)
	{
		for (size_t i = 0; i < COUNTOF(keySettings); ++i)
		{
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

		/* Existing 0 and 1 values map to Off and Bosses. */
		config_get_int_option(section, "boss_vulnerable_cue", &vulnerableCue);
		if (vulnerableCue < VULN_CUE_OFF || vulnerableCue >= VULN_CUE_COUNT)
			vulnerableCue = VULN_CUE_BOSSES;

		load_int_flag(section, "armor_alarm", &armorAlarm);
		load_int_flag(section, "link_sounds", &linkSounds);

		config_get_int_option(section, "sfx_filter", &sfx_filter);
		if (sfx_filter < SFX_FILTER_OFF || sfx_filter >= SFX_FILTER_COUNT)
			sfx_filter = SFX_FILTER_OFF;
		config_get_int_option(section, "sfx_copy_limit", &sfx_copy_limit);
		if (sfx_copy_limit < 0 || sfx_copy_limit > SFX_COPY_LIMIT_MAX)
			sfx_copy_limit = 0;
		config_get_int_option(section, "rumble", &rumble_strength);
		if (rumble_strength < RUMBLE_OFF || rumble_strength >= RUMBLE_COUNT)
			rumble_strength = RUMBLE_OFF;
		load_int_flag(section, "joycons_separate", &joycons_separate);

		load_int_flag(section, "debug_mode", &debugMode);

		int hang_timeout = crashlog_get_hang_timeout();
		config_get_int_option(section, "hang_timeout", &hang_timeout);
		crashlog_set_hang_timeout(hang_timeout);  // clamps into range

		int net_log_enabled = crashlog_get_netlog_enabled() ? 1 : 0;
		config_get_int_option(section, "net_log", &net_log_enabled);
		crashlog_set_netlog_enabled(net_log_enabled != 0);

		load_int_flag(section, "enemy_bars", &enemyBars);

		config_get_int_option(section, "enemy_bar_layout", &enemyBarLayout);
		config_get_int_option(section, "enemy_bar_position", &enemyBarPosition);
		config_get_int_option(section, "enemy_bar_opacity", &enemyBarOpacity);

		// The missing key means Off.
		int custom_endless = CUSTOM_ENDLESS_OFF;
		config_get_int_option(section, "custom_endless", &custom_endless);
		customEndlessMode = (custom_endless >= CUSTOM_ENDLESS_OFF && custom_endless < CUSTOM_ENDLESS_MODES)
		                  ? custom_endless : CUSTOM_ENDLESS_OFF;

		load_int_flag(section, "smooth_motion", &smoothMotion);
		load_int_flag(section, "extra_sparks", &extraSparks);
		load_int_flag(section, "extra_parallax", &extraParallax);
		load_int_flag(section, "mirrored_layers", &mirroredLayers);
		load_int_flag(section, "touch_sidekick_buttons", &touchSidekickButtons);
		load_int_flag(section, "touch_nav_buttons", &touchNavButtons);

		config_get_int_option(section, "touch_button_opacity", &touchButtonOpacity);
		if (touchButtonOpacity < 0 || touchButtonOpacity > TOUCH_OPACITY_MAX)
			touchButtonOpacity = TOUCH_OPACITY_DEFAULT;

		// Builds without MIDI fall back to OPL during audio initialization.
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

		config_get_int_option(section, "custom_sounds", &custom_sounds);
		if (custom_sounds < CUSTOM_SOUNDS_OFF || custom_sounds >= CUSTOM_SOUNDS_COUNT)
			custom_sounds = CUSTOM_SOUNDS_OFF;

		// Store the join target and local listen port separately.
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

			// Host-selected rollback or delay-based lockstep.
			config_get_bool_option(section, "net_rollback", &net_rollback);

			// Repair a detected desync by streaming the host's state to the
			// joiner (net_rollback.h).  Host-authoritative; rollback sessions only.
			config_get_bool_option(section, "net_desync_recovery", &net_desync_recovery);

			// Online Campaign: pay every kill and score pickup to both players
			// (player.h).  Host-authoritative; Campaign sessions only.
			config_get_bool_option(section, "net_campaign_shared_credit", &coopSharedCredit);

			// Keep the historical key name for config compatibility.
			config_get_bool_option(section, "net_coop_double_pickups", &coopDoubleEarnings);
			config_get_bool_option(section, "net_endless_combo_shared", &network_host_endless_combo_shared);

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

		load_int_flag(section, "gauge_flash_shield", &gaugeFlashShield);
		load_int_flag(section, "gauge_flash_armor", &gaugeFlashArmor);
		load_int_flag(section, "special_screen_tint", &specialScreenTint);

		config_get_int_option(section, "zica_l11_base", &zicaLaserBase);
		if (zicaLaserBase < 0 || zicaLaserBase >= ZICA_BASE_COUNT)
			zicaLaserBase = ZICA_BASE_EP4;

		reworkConfigLoad(section);
		load_int_flag(section, "charge_laser_cannon", &chargeLaserCannon);
		load_int_flag(section, "restore_base_dispensers", &restoreBaseDispensers);
		load_int_flag(section, "arcade_life_boost", &arcadeLifeBoost);
		load_int_flag(section, "arcade_random_balls", &arcadeRandomBalls);
		load_int_flag(section, "arcade_rear_gun_scale", &arcadeRearGunScale);
		load_int_flag(section, "unused_shop_sprites", &unusedShopSprites);
		load_int_flag(section, "centered_shot_hitboxes", &centeredShotHitboxes);
		load_int_flag(section, "guided_shot_screen_aim", &guidedShotScreenAim);
		load_int_flag(section, "ship_editor_stars", &shipEditorStars);
		weaponEditorConfigLoad(section);

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

		customWeaponEditLevel = 0;
		customWeaponEditMode = 0;
	}

	section = config_find_section(config, "game", NULL);
	if (section != NULL)
	{
		int v;
		if (config_get_int_option(section, "music_volume", &v) && v >= 0 && v <= 255)
			tyrMusicVolume = (JE_word)v;
		if (config_get_int_option(section, "sound_volume", &v) && v >= 0 && v <= 255)
			fxVolume = (JE_word)v;
		if (config_get_int_option(section, "game_speed", &v) && v >= 1 && v <= 5)
			gameSpeed = (JE_byte)v;
		if (config_get_int_option(section, "detail_level", &v) && v >= 1 && v <= 6)
			processorType = (JE_byte)v;
		if (config_get_int_option(section, "gamma", &v) && v >= 0 && v <= 4)
			gammaCorrection = (JE_byte)v;
		if (config_get_int_option(section, "difficulty", &v) && v >= DIFFICULTY_WIMP && v <= DIFFICULTY_10)
			difficultyLevel = (JE_shortint)v;
		if (config_get_int_option(section, "background2", &v))
			background2 = v != 0;
		if (config_get_int_option(section, "input_p1", &v) && v >= 0 && v <= 255)
			inputDevice[0] = (JE_byte)v;
		if (config_get_int_option(section, "input_p2", &v) && v >= 0 && v <= 255)
			inputDevice[1] = (JE_byte)v;
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
	config_set_int_option(section, "crt_filter", crt_filter);
	config_set_int_option(section, "crt_strength", crt_strength);
	config_set_int_option(section, "crt_curvature", crt_curvature);
	config_set_int_option(section, "crt_mask", crt_mask);
	config_set_int_option(section, "crt_corners", crt_corners);
	config_set_int_option(section, "crt_rolling_bar", crt_rolling_bar);

	config_set_int_option(section, SHIP_SENS_CFG, ship_sensitivity);

	config_set_int_option(section, "render_supersample", render_supersample);

	config_set_int_option(section, "smoothie_full_res", smoothie_full_res ? 1 : 0);

	config_set_int_option(section, "render_threads", render_threads);

	section = config_find_or_add_section(config, "keyboard", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory

	for (size_t i = 0; i < COUNTOF(keySettings); ++i)
	{
		const char *scancodeName = SDL_GetScancodeName(keySettings[i]);
		if (scancodeName[0] == '\0')
			scancodeName = NULL;
		config_set_string_option(section, keySettingNames[i], scancodeName);
	}

	// Best-effort: the directory almost always exists already, and a genuine failure surfaces at the
	// fopen below rather than here.
	make_user_directory();

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
	config_set_int_option(section, "sfx_filter", sfx_filter);
	config_set_int_option(section, "sfx_copy_limit", sfx_copy_limit);
	config_set_int_option(section, "rumble", rumble_strength);
	config_set_int_option(section, "joycons_separate", joycons_separate ? 1 : 0);
	config_set_int_option(section, "debug_mode", debugMode ? 1 : 0);
	config_set_int_option(section, "hang_timeout", crashlog_get_hang_timeout());
	config_set_int_option(section, "net_log", crashlog_get_netlog_enabled() ? 1 : 0);
	config_set_int_option(section, "enemy_bars", enemyBars ? 1 : 0);
	config_set_int_option(section, "enemy_bar_layout", enemyBarLayout);
	config_set_int_option(section, "enemy_bar_position", enemyBarPosition);
	config_set_int_option(section, "enemy_bar_opacity", enemyBarOpacity);
	// Omit the default so clearing custom episodes removes the key.
	if (customEndlessMode != CUSTOM_ENDLESS_OFF)
		config_set_int_option(section, "custom_endless", customEndlessMode);
	else
		config_remove_option(section, "custom_endless");
	config_set_int_option(section, "smooth_motion", smoothMotion ? 1 : 0);
	config_set_int_option(section, "extra_sparks", extraSparks ? 1 : 0);
	config_set_int_option(section, "extra_parallax", extraParallax ? 1 : 0);
	config_set_int_option(section, "mirrored_layers", mirroredLayers ? 1 : 0);
	config_set_int_option(section, "touch_sidekick_buttons", touchSidekickButtons ? 1 : 0);
	config_set_int_option(section, "touch_nav_buttons", touchNavButtons ? 1 : 0);
	config_set_int_option(section, "touch_button_opacity", touchButtonOpacity);
	config_set_string_option(section, "music_device", music_device_names[music_device]);
	config_set_int_option(section, "custom_sounds", custom_sounds);

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
	config_set_int_option(section, "boss_vulnerable_cue", vulnerableCue);
	config_set_int_option(section, "gauge_flash_shield", gaugeFlashShield ? 1 : 0);
	config_set_int_option(section, "gauge_flash_armor", gaugeFlashArmor ? 1 : 0);
	config_set_int_option(section, "special_screen_tint", specialScreenTint ? 1 : 0);
	config_set_int_option(section, "zica_l11_base", zicaLaserBase);
	reworkConfigSave(section);
	config_set_int_option(section, "charge_laser_cannon", chargeLaserCannon ? 1 : 0);
	config_set_int_option(section, "restore_base_dispensers", restoreBaseDispensers ? 1 : 0);
	config_set_int_option(section, "arcade_life_boost", arcadeLifeBoost ? 1 : 0);
	config_set_int_option(section, "arcade_random_balls", arcadeRandomBalls ? 1 : 0);
	config_set_int_option(section, "arcade_rear_gun_scale", arcadeRearGunScale ? 1 : 0);
	config_set_int_option(section, "unused_shop_sprites", unusedShopSprites ? 1 : 0);
	config_set_int_option(section, "centered_shot_hitboxes", centeredShotHitboxes ? 1 : 0);
	config_set_int_option(section, "guided_shot_screen_aim", guidedShotScreenAim ? 1 : 0);
	config_set_int_option(section, "ship_editor_stars", shipEditorStars ? 1 : 0);
	weaponEditorConfigSave(section);
	enhancementSaveCustomSet(section);
	config_set_int_option(section, "xmas", xmasMode);

	config_remove_option(section, "custom_weapon_enabled");

	for (int i = 0; i < expertSettingsCount; ++i)
		config_set_int_option(section, expertSettings[i].cfgKey, *expertSettings[i].value);

	section = config_find_or_add_section(config, "game", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory
	config_set_int_option(section, "music_volume", tyrMusicVolume);
	config_set_int_option(section, "sound_volume", fxVolume);
	config_set_int_option(section, "game_speed", gameSpeed);
	config_set_int_option(section, "detail_level", processorType);
	config_set_int_option(section, "gamma", gammaCorrection);
	config_set_int_option(section, "difficulty", difficultyLevel);
	config_set_int_option(section, "background2", background2 ? 1 : 0);
	config_set_int_option(section, "input_p1", inputDevice[0]);
	config_set_int_option(section, "input_p2", inputDevice[1]);

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

bool configuration_loaded = false;

void JE_loadConfiguration(void)
{
	// The DOS defaults, then opentyrian.cfg over them.
	background2 = true;
	tyrMusicVolume = 191;
	fxVolume = 191;
	gammaCorrection = 0;
	processorType = 4;  // detail level "Pentium"
	gameSpeed = 4;

	load_opentyrian_config();

	if (tyrMusicVolume > 255)
		tyrMusicVolume = 255;
	if (fxVolume > 255)
		fxVolume = 255;

	set_volume(tyrMusicVolume, fxVolume);

	bool imported = false;
	if (!save_file_load())
	{
		imported = legacy_save_load();
		if (!imported)
			save_defaults();
	}

	JE_initProcessorType();
	configuration_loaded = true;

	// Write the imported state in the new form at once, so the migration does not wait on a save.
	if (imported)
		JE_saveConfiguration();
}

void JE_saveConfiguration(void)
{
	// Transfer adoption checks write failures; existing save callers remain best-effort.
	if (!configuration_loaded)
		return;
	(void)save_file_write_current();
	(void)save_opentyrian_config();
}
