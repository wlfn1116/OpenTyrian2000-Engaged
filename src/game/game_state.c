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
#include "game_state.h"

#include <stdlib.h>

#include "config.h"
#include "endless.h"
#include "episodes.h"
#include "explosions.h"
#include "hud.h"
#include "net_style.h"
#include "network.h"
#include "opentyr.h"
#include "rollback.h"
#include "sfx.h"
#include "sparks.h"
#include "sprite.h"
#include "video.h"

JE_integer tempDat, tempDat2, tempDat3;

const JE_byte SANextShip[SA + 2] /* [0..SA + 1] */ = { 3, 8, 6, 2, 5, 1, 4, 10, 9, 7, 3 };
const JE_word SASpecialWeapon[SA] /* [1..SA] */  = { 7, 8, 9, 10, 11, 12, 13, 48, 47 };
const JE_word SASpecialWeaponB[SA] /* [1..SA] */ = {37, 6, 15, 40, 16, 14, 41, 48, 47 };
const JE_byte SAShip[SA] /* [1..SA] */ = { 3, 1, 5, 10, 2, 11, 12, 15, 17 };
const JE_word SAWeapon[SA][5] /* [1..SA, 1..5] */ =
{  /*  R  Bl  Bk  G   P */
	{  9, 31, 32, 33, 34 },  /* Stealth Ship */
	{ 19,  8, 22, 41, 34 },  /* StormWind    */
	{ 27,  5, 20, 42, 31 },  /* Techno       */
	{ 15,  3, 28, 22, 12 },  /* Enemy        */
	{ 23, 35, 25, 14,  6 },  /* Weird        */
	{  2,  5, 21,  4,  7 },  /* Unknown      */
	{ 40, 38, 37, 41, 36 },  /* NortShip Z   */
	{ 47, 45, 19, 33, 19 },  /* Dragon       */
	{ 44, 26, 46, 26,  1 }   /* Pretzel Pete */
};

const JE_byte specialArcadeWeapon[PORT_NUM] /* [1..Portnum] */ =
{
	17,17,18,0,0,0,10,0,0,0,0,0,44,0,10,0,19,0,0,-0,0,0,0,0,0,0,
	-0,0,0,0,45,0,0,0,0,0,0,0,0,0,0,0
};

const JE_byte optionSelect[16][3][2] /* [0..15, 1..3, 1..2] */ =
{	/*  MAIN    OPT    FRONT */
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ { 1, 1},{16,16},{30,30} },  /*Single Shot*/
	{ { 2, 2},{29,29},{29,20} },  /*Dual Shot*/
	{ { 3, 3},{21,21},{12, 0} },  /*Charge Cannon*/
	{ { 4, 4},{18,18},{16,23} },  /*Vulcan*/
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ { 6, 6},{29,16},{ 0,22} },  /*Super Missile*/
	{ { 7, 7},{19,19},{19,28} },  /*Atom Bomb*/
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ {10,10},{21,21},{21,27} },  /*Mini Missile*/
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ {13,13},{17,17},{13,26} },  /*MicroBomb*/
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ {15,15},{15,16},{15,16} }   /*Post-It*/
};

const JE_word PGR[21] /* [1..21] */ =
{
	4,
	1,2,3,
	41-21,57-21,73-21,89-21,105-21,
	121-21,137-21,153-21,
	151,151,151,151,73-21,73-21,1,2,4
	/*151,151,151*/
};
const JE_byte PAni[21] /* [1..21] */ = {1,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1};

const JE_word linkGunWeapons[38] /* [1..38] */ =
{
	0,0,0,0,0,0,0,0,444,445,446,447,0,448,449,0,0,0,0,0,450,451,0,506,0,564,
	  445,446,447,448,449,445,446,447,448,449,450,451
};
const JE_word chargeGunWeapons[38] /* [1..38] */ =
{
	0,0,0,0,0,0,0,0,476,458,464,482,0,488,470,0,0,0,0,0,494,500,0,528,0,558,
	  458,458,458,458,458,458,458,458,458,458,458,458
};
const JE_byte randomEnemyLaunchSounds[3] /* [1..3] */ = {13,6,26};

/* YKS: Twiddle cheat sheet:
 * 1: UP
 * 2: DOWN
 * 3: LEFT
 * 4: RIGHT
 * 5: UP+FIRE
 * 6: DOWN+FIRE
 * 7: LEFT+FIRE
 * 8: RIGHT+FIRE
 * 9: Release all keys (directions and fire)
 */
const JE_byte keyboardCombos[26][8] /* [1..26, 1..8] */ =
{
	{ 2, 1,   2,   5, 137,           0, 0, 0}, /*Invulnerability*/
	{ 4, 3,   2,   5, 138,           0, 0, 0}, /*Atom Bomb*/
	{ 3, 4,   6, 139,             0, 0, 0, 0}, /*Seeker Bombs*/
	{ 2, 5, 142,               0, 0, 0, 0, 0}, /*Ice Blast*/
	{ 6, 2,   6, 143,             0, 0, 0, 0}, /*Auto Repair*/
	{ 6, 7,   5,   8,   6,   7,  5, 112     }, /*Spin Wave*/
	{ 7, 8, 101,               0, 0, 0, 0, 0}, /*Repulsor*/
	{ 1, 7,   6, 146,             0, 0, 0, 0}, /*Protron Field*/
	{ 8, 6,   7,   1, 120,           0, 0, 0}, /*Minefield*/
	{ 3, 6,   8,   5, 121,           0, 0, 0}, /*Post-It Blast*/
	{ 1, 2,   7,   8, 119,           0, 0, 0}, /*Drone Ship - TBC*/
	{ 3, 4,   3,   6, 123,           0, 0, 0}, /*Repair Player 2*/
	{ 6, 7,   5,   8, 124,           0, 0, 0}, /*Super Bomb - TBC*/
	{ 1, 6, 125,               0, 0, 0, 0, 0}, /*Hot Dog*/
	{ 9, 5, 126,               0, 0, 0, 0, 0}, /*Lightning UP      */
	{ 1, 7, 127,               0, 0, 0, 0, 0}, /*Lightning UP+LEFT */
	{ 1, 8, 128,               0, 0, 0, 0, 0}, /*Lightning UP+RIGHT*/
	{ 9, 7, 129,               0, 0, 0, 0, 0}, /*Lightning    LEFT */
	{ 9, 8, 130,               0, 0, 0, 0, 0}, /*Lightning    RIGHT*/
	{ 4, 2,   3,   5, 131,           0, 0, 0}, /*Warfly            */
	{ 3, 1,   2,   8, 132,           0, 0, 0}, /*FrontBlaster      */
	{ 2, 4,   5, 133,             0, 0, 0, 0}, /*Gerund            */
	{ 3, 4,   2,   8, 134,           0, 0, 0}, /*FireBomb          */
	{ 1, 4,   6, 135,             0, 0, 0, 0}, /*Indigo            */
	{ 1, 3,   6, 137,             0, 0, 0, 0}, /*Invulnerability [easier] */
	{ 1, 4,   3,   4,   7, 136,         0, 0}  /*D-Media Protron Drone    */
};

const JE_byte shipCombosB[21] /* [1..21] */ =
	{15,16,17,18,19,20,21,22,23,24, 7, 8, 5,25,14, 4, 6, 3, 9, 2,26};
  /*!! SUPER Tyrian !!*/
const JE_byte superTyrianSpecials[4] /* [1..4] */ = {1,2,4,5};

const JE_byte shipCombos[19][3] /* [0..12, 1..3] */ =
{
	{ 5, 4, 7},  /*2nd Player ship*/
	{ 1, 2, 0},  /*USP Talon*/
	{14, 4, 0},  /*Super Carrot*/
	{ 4, 5, 0},  /*Gencore Phoenix*/
	{ 6, 5, 0},  /*Gencore Maelstrom*/
	{ 7, 8, 0},  /*MicroCorp Stalker*/
	{ 7, 9, 0},  /*MicroCorp Stalker-B*/
	{10, 3, 5},  /*Prototype Stalker-C*/
	{ 5, 8, 9},  /*Stalker*/
	{ 1, 3, 0},  /*USP Fang*/
	{ 7,16,17},  /*U-Ship*/
	{ 2,11,12},  /*1st Player ship*/
	{ 3, 8,10},  /*Nort ship*/
	{ 0, 0, 0},  // Dummy entry added for Stalker 21.126
	{ 1, 0, 0},  /*Storm*/
	{ 4, 0, 0},  /*Red Dragon*/
	{ 5, 9, 2},  /*Gencore II*/
	{ 0, 0, 0},  /*PeteZoomer*/
	{ 0, 0, 0}   /*Rum Bottle*/
};

/*Street-Fighter Commands*/
JE_byte SFCurrentCode[2][21]; /* [1..2, 1..21] */
JE_byte SFExecuted[2]; /* [1..2] */

/*Special General Data*/
JE_byte lvlFileNum;
// One-shot level-file override for sections with more than one `]L`. Selection paths set it and
// JE_loadMap consumes it; zero selects the section default.
JE_byte forcedLvlFileNum = 0;
JE_word maxEvent, eventLoc;
/*JE_word maxenemies;*/
JE_word tempBackMove, explodeMove; /*Speed of background movement*/
JE_byte levelEnd;
JE_word levelEndFxWait;
JE_shortint levelEndWarp;
JE_boolean endLevel, reallyEndLevel, waitToEndLevel, playerEndLevel,
           normalBonusLevelCurrent, bonusLevelCurrent,
           smallEnemyAdjust, readyToEndLevel, quitRequested;

JE_byte newPL[10]; /* [0..9] */ /*Eventsys event 75 parameter*/
JE_word returnLoc;
JE_boolean returnActive;
JE_word galagaShotFreq;
JE_longint galagaLife;

JE_boolean debug = false; /*Debug Mode*/
Uint32 debugTime, lastDebugTime;
JE_longint debugHistCount;
JE_real debugHist;
JE_word curLoc; /*Current Pixel location of background 1*/

JE_boolean firstGameOver, gameLoaded, enemyStillExploding;

/* Destruction Ratio */
JE_word totalEnemy;
JE_word enemyKilled;

/* Shape/Map Data - All in one Segment! */
struct JE_MegaDataType1 megaData1;
struct JE_MegaDataType2 megaData2;
struct JE_MegaDataType3 megaData3;

/* Secret Level Display */
JE_byte flash;
JE_shortint flashChange;
JE_byte displayTime;

/* Sound Effects Queue */
JE_byte soundQueue[8]; /* [0..7] */

/*Level Event Data*/
JE_boolean enemyContinualDamage;
JE_boolean enemiesActive;
JE_boolean forceEvents;
JE_boolean stopBackgrounds;
JE_byte stopBackgroundNum;
JE_byte damageRate;  /*Rate at which a player takes damage*/
JE_boolean background3x1;  /*Background 3 enemies use Background 1 X offset*/
JE_boolean background3x1b; /*Background 3 enemies moved 8 pixels left*/

JE_boolean levelTimer;
JE_word    levelTimerCountdown;
JE_word    levelTimerJumpTo;
JE_boolean randomExplosions;

JE_boolean editShip1, editShip2;

JE_boolean globalFlags[10]; /* [1..10] */
JE_byte levelSong;

/* DESTRUCT game */
JE_boolean loadDestruct;

/* MapView Data */
JE_word mapOrigin, mapPNum;
JE_byte mapPlanet[5], mapSection[5]; /* [1..5] */

/* Interface Constants */
JE_boolean moveTyrianLogoUp;
JE_boolean skipStarShowVGA;

/*EnemyData*/
JE_MultiEnemyType enemy;
JE_EnemyAvailType enemyAvail;  /* values: 0: used, 1: free, 2: secret pick-up */
JE_word enemyOffset;
JE_word enemyOnScreen;
// On-screen enemies parked above the map with no path back in.
JE_word enemyParkedAbove;
JE_word mapStopStallTicks;  // ticks a scripted map stop has been held only by parked-above enemies
JE_word superEnemy254Jump;

/*EnemyShotData*/
JE_boolean fireButtonHeld;
JE_boolean enemyShotAvail[ENEMY_SHOT_MAX]; /* [1..Enemyshotmax] */
EnemyShotType enemyShot[ENEMY_SHOT_MAX]; /* [1..Enemyshotmax]  */

/* Player Shot Data */
JE_byte     zinglonDuration;
JE_byte     zinglonRamp;  /* how far the pillar has opened so far, 0..ZINGLON_PILLAR_HALF_W */

/* Per-ship Soul of Zinglon render request. The display pass places the pillar at
 * the render-rate ship position; an explicit flag distinguishes spent duration 1. */
bool        zinglonPillarActive[2] = { false, false };
int         zinglonPillarCX[2] = { 0, 0 };    /* pillar centre x in game_screen coords */
int         zinglonPillarTemp[2] = { 0, 0 };  /* pillar half-width */

JE_byte     astralDuration;
JE_word     flareDuration;
JE_boolean  flareStart;
JE_shortint flareColChg;
/* The rollback-registered flare grade is separate from the level's screen filter. */
bool        flareOwnsFilter = false;
JE_byte     specialWait;
JE_byte     nextSpecialWait;
JE_boolean  spraySpecial;
JE_byte     doIced;
JE_boolean  infiniteShot;
JE_boolean  cheatInfiniteSidekickAmmo = false;
JE_boolean  cheatInfiniteShields = false;
JE_boolean  cheatInfiniteArmor = false;
JE_boolean  cheatInfiniteGenerator = false;  /* debug: weapons don't drain generator power */
JE_boolean  cheatNoEnemyFire = false;
JE_boolean  cheatInstantCharge = false;  /* debug: charge sidekicks reach full charge instantly */
JE_byte     noclipMode = NOCLIP_OFF;     /* debug: pass through enemies (see enum in game_state.h) */
JE_boolean  debugHitboxOverlay = false;
JE_boolean  debugPerfOverlay = false;
JE_boolean  autoFireSpecial = false;
JE_byte     debugTwiddleSpecial = 0;       /* debug: selected twiddle's special index (0 = none) */
JE_boolean  debugAutofireTwiddle = false;  /* debug: auto-fire the selected twiddle while fire is held */
JE_boolean  debugTwiddleTrigger = false;   /* debug: one-shot "fire the twiddle now" request from the menu */
JE_boolean  debugToggleFire = false;       /* debug: fire button toggles auto-fire instead of hold-to-fire */
JE_boolean  debugToggleFireActive = false; /* debug: the Toggle Fire latch; ship is currently auto-firing */
JE_byte     chargeSidekickAutofire = CHARGE_AUTOFIRE_ON;  /* default On; edited by the debug menu + the Weapons menu */
JE_boolean  dispenserBasesActive = false;  /* this level wakes the dormant dispenser bases (set at level start) */
JE_boolean  difficultyAdjust = true;
JE_boolean  expertMode = false;

int expertBossHpMult      = EXPERT_DEF_BOSS_HP;
int expertEnemyArmorPct   = EXPERT_DEF_ENEMY_ARMOR;
int expertEnergyPct       = EXPERT_DEF_ENERGY;
int expertShopCostMult    = EXPERT_DEF_SHOP_COST;
int expertUpgradeCostMult = EXPERT_DEF_UPGRADE_COST;
int expertScorePct        = EXPERT_DEF_CASH;

ExpertSetting expertSettings[] =
{
	{ "Boss HP",       "expert_boss_hp",      &expertBossHpMult,        1,  25, 1, EXPERT_DEF_BOSS_HP,      'x' },
	{ "Enemy Armor",   "expert_enemy_armor",  &expertEnemyArmorPct,   100, 300, 5, EXPERT_DEF_ENEMY_ARMOR, '%' },
	{ "Weapon Energy", "expert_energy",       &expertEnergyPct,       100, 300, 5, EXPERT_DEF_ENERGY,      '%' },
	{ "Shop Cost",     "expert_shop_cost",    &expertShopCostMult,      1,  20, 1, EXPERT_DEF_SHOP_COST,   'x' },
	{ "Upgrade Cost",  "expert_upgrade_cost", &expertUpgradeCostMult,   1,  20, 1, EXPERT_DEF_UPGRADE_COST,'x' },
	{ "Cash Bonus",    "expert_cash",         &expertScorePct,        100, 400, 5, EXPERT_DEF_CASH,        '%' },
};
const int expertSettingsCount = (int)(sizeof(expertSettings) / sizeof(expertSettings[0]));

// The debug-sync block has a fixed slot count. Keep every expert setting inside it.
COMPILE_TIME_ASSERT(expert_settings_fit_debug_sync,
                    COUNTOF(expertSettings) <= NETWORK_EXPERT_SLOTS);

void clamp_expert_settings(void)
{
	for (int i = 0; i < expertSettingsCount; ++i)
	{
		ExpertSetting* s = &expertSettings[i];
		if (*s->value < s->lo) *s->value = s->lo;
		if (*s->value > s->hi) *s->value = s->hi;
	}
}

/*PlayerData*/
JE_boolean allPlayersGone; /*Both players dead and finished exploding*/

const uint shadowYDist = 10;

JE_real optionSatelliteRotate;

JE_integer optionAttachmentMove[2];                               // per sidekick slot (LEFT/RIGHT)
JE_boolean optionAttachmentLinked[2], optionAttachmentReturn[2];  // so both front options can launch

JE_byte chargeWait, chargeLevel, chargeMax, chargeGr, chargeGrWait;

JE_word neat;

/*Temporary Numbers*/
JE_byte temp, temp2, temp3;
JE_word tempW;

JE_boolean doNotSaveBackup;

JE_word x, y;
JE_integer b;

JE_byte **BKwrap1to, **BKwrap2to, **BKwrap3to,
        **BKwrap1, **BKwrap2, **BKwrap3;

JE_shortint specialWeaponFilter, specialWeaponFreq;
JE_word     specialWeaponWpn;
JE_boolean  linkToPlayer;

JE_word shipGr, shipGr2;
Sprite2_array *shipGrPtr, *shipGr2ptr;

void JE_resetPlayerOptions(Player *this_player)
{
	// Personal perks: Ordnance Reserves and Rapid Recharge size this ship's own magazines.
	const uint fxSaved = endlessFxPlayer();
	endlessSetFxPlayer((uint)(this_player - player));

	for (uint i = 0; i < COUNTOF(this_player->sidekick); ++i)
	{
		JE_OptionType *this_option = &options[this_player->items.sidekick[i]];

		// Scale refill cadence so larger Ordnance Reserves magazines fill in the stock time.
		this_player->sidekick[i].ammo =
		this_player->sidekick[i].ammo_max = endlessPerkSidekickAmmo(this_option->ammo);

		this_player->sidekick[i].ammo_refill_ticks =
		this_player->sidekick[i].ammo_refill_ticks_max =
			endlessPerkSidekickRefillTicks((105 - this_option->ammo) * 4, this_option->ammo);

		this_player->sidekick[i].style = this_option->tr;

		this_player->sidekick[i].animation_enabled = (this_option->option == 1);
		this_player->sidekick[i].animation_frame = 0;

		this_player->sidekick[i].charge = 0;
		this_player->sidekick[i].charge_ticks = endlessPerkChargeTicks(20);
	}

	endlessSetFxPlayer(fxSaved);
}

void JE_drawOptions(void)
{
	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg;

	JE_labelAmmoSidekicks();  // keep the shop names in step with the magazines we're about to load

	const uint first_player = dual_ship_mode() ? 0 : (twoPlayerMode ? 1 : 0);
	const uint last_player = dual_ship_mode() ? COUNTOF(player) : first_player + 1;
	for (uint p = first_player; p < last_player; ++p)
		JE_resetPlayerOptions(&player[p]);

	JE_drawOptionsHUD();

	VGAScreen = temp_surface;

	JE_drawOptionLevel();
}

/* Which ships carry the Endless effect layer: both of them in an online co-op run, player 1
 * otherwise (the campaign debug layer only ever equips one). */
static bool endlessFxShip(const Player *this_player)
{
	return endlessFxActive() && (coopEndlessMode || this_player == &player[0]);
}

/* Damage sites read the struck ship's generator reserve, not the weapon code's power scratch. */
static int endlessGeneratorGet(const Player *this_player)
{
	return dual_ship_mode() ? (int)this_player->generator_power : (int)power;
}

static void endlessGeneratorSet(Player *this_player, int v)
{
	if (v < 0)
		v = 0;
	if (v > 900)
		v = 900;
	if (dual_ship_mode())
		this_player->generator_power = (Uint16)v;
	else
		power = (uint)v;
}

/* Rewind both the ordinary special cadence and the flare-imposed wait. */
static void endlessKineticCoolSpecial(Player *this_player)
{
	JE_byte *const clocks[2] = {
		dual_ship_mode() ? &this_player->shot_repeat[SHOT_SPECIAL] : &shotRepeat[SHOT_SPECIAL],
		dual_ship_mode() ? &this_player->special_wait              : &specialWait,
	};

	for (uint i = 0; i < COUNTOF(clocks); ++i)
		*clocks[i] -= (JE_byte)endlessPerkKineticCooldownCut(*clocks[i]);
}

/* Feed sidekick magazines and charge ramps; mark event-drawn ammo gauges dirty. */
static void endlessKineticFeedSidekicks(Player *this_player, int rounds, int stages)
{
	bool refilled = false;

	for (uint i = 0; i < COUNTOF(this_player->sidekick); ++i)
	{
		const int cap = this_player->sidekick[i].ammo_max;
		if (rounds > 0 && cap > 0 && this_player->sidekick[i].ammo < cap)
		{
			this_player->sidekick[i].ammo = MIN(this_player->sidekick[i].ammo + rounds, cap);
			refilled = true;
		}

		// options[].pwr is the pod's top charge stage; 0 means it has no ramp to advance.
		const uint stageCap = options[this_player->items.sidekick[i]].pwr;
		if (stages > 0 && this_player->sidekick[i].charge < stageCap)
			this_player->sidekick[i].charge =
				MIN(this_player->sidekick[i].charge + (uint)stages, stageCap);
	}

	if (refilled && (uint)(this_player - player) == hud_sidekick_player_index())
		hud_sidekicks_dirty = true;
}

// Clear enemy shots within this ship's Countermeasures reach. Return the number cleared.
int endlessCountermeasureBurst(Player *this_player)
{
	const int cmRadius = endlessPerkCountermeasureRadius();
	if (cmRadius <= 0)
		return 0;

	// The perk's radius begins at the hull edge, not the ship's centre.
	const int reachX = (int)this_player->shot_hit_area_x + cmRadius;
	const int reachY = (int)this_player->shot_hit_area_y + cmRadius;

	int swept = 0;
	for (int es = 0; es < ENEMY_SHOT_MAX; ++es)
	{
		if (!enemyShotAvail[es]
		    && abs(enemyShot[es].sx - this_player->x) <= reachX
		    && abs(enemyShot[es].sy - this_player->y) <= reachY)
		{
			enemy_shot_vaporise_sparks(es);
			JE_setupExplosion(enemyShot[es].sx, enemyShot[es].sy, 0, 0, false, false);
			enemyShotAvail[es] = true;
			++swept;
		}
	}

	// Announce only a burst that cleared at least one shot.
	if (swept > 0)
	{
		JE_setupExplosion(this_player->x - reachX, this_player->y, 0, 0, false, false);
		JE_setupExplosion(this_player->x + reachX, this_player->y, 0, 0, false, false);
		JE_setupExplosion(this_player->x, this_player->y - reachY, 0, 0, false, false);
		JE_setupExplosion(this_player->x, this_player->y + reachY, 0, 0, false, false);
		soundQueue[4] = S_WEAPON_7;  // point-defense "thunk"
	}

	return swept;
}

static void shield_hit_ring(const Player *this_player)
{
	JE_setupExplosion(this_player->x - 17, this_player->y - 12, 0, 14, false, !twoPlayerMode);
	JE_setupExplosion(this_player->x - 5 , this_player->y - 12, 0, 15, false, !twoPlayerMode);
	JE_setupExplosion(this_player->x + 7 , this_player->y - 12, 0, 16, false, !twoPlayerMode);
	JE_setupExplosion(this_player->x + 19, this_player->y - 12, 0, 17, false, !twoPlayerMode);

	JE_setupExplosion(this_player->x - 17, this_player->y + 2 , 0, 18, false, !twoPlayerMode);
	JE_setupExplosion(this_player->x + 19, this_player->y + 2 , 0, 19, false, !twoPlayerMode);

	JE_setupExplosion(this_player->x - 17, this_player->y + 16, 0, 20, false, !twoPlayerMode);
	JE_setupExplosion(this_player->x - 5 , this_player->y + 16, 0, 21, false, !twoPlayerMode);
	JE_setupExplosion(this_player->x + 7 , this_player->y + 16, 0, 22, false, !twoPlayerMode);
}

JE_byte JE_playerDamage(JE_byte temp,
                        Player *this_player)
{
	// Nitro and the rest of the personal deals belong to the ship being hit.
	endlessSetFxPlayer((uint)(this_player - &player[0]));

	int playerDamage = 0;
	soundQueue[7] = S_SHIELD_HIT;

	const int oldShield = this_player->shield;
	const int oldArmor  = this_player->armor;

	if (cheatInfiniteShields)
		return 0;

	// Endless Bulwark relic: soften each incoming hit by a flat amount, but always leave at
	// least 1 damage (only the main player carries perks; a lone hit of 0 is left untouched).
	if (endlessFxShip(this_player) && temp > 1)
	{
		int t = (int)temp - endlessPlayerDamageReduce();
		temp = (t < 1) ? 1 : (JE_byte)t;
	}

	// Nitro (gamble deal): the hull is stripped for raw firepower, so any hit that lands is fatal.
	if (endlessFxShip(this_player) && (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_NITRO))
		temp = 255;

	// Countermeasure Suite arms only when damage reaches the hull.
	bool cmHullHit = false;

	/* Player Damage Routines */
	if (this_player->shield < temp)
	{
		playerDamage = temp;
		temp -= this_player->shield;
		this_player->shield = 0;

		// Aegis Gate spends the remaining shield and blocks overflow before hull-hit
		// effects. The helper also starts its cooldown.
		if (endlessFxShip(this_player) && temp > 0
		    && endlessAegisGateConsume(oldShield, temp))
		{
			temp = 0;
			// Mark a block with the full shield ring and its unique clink sound.
			shield_hit_ring(this_player);
			soundQueue[4] = S_CLINK;  // Deflection cue.
		}

		if (temp > 0)
			cmHullHit = true;

		if (temp > 0 && !cheatInfiniteArmor)
		{
			/*Through Shields - Now Armor */

			if (this_player->armor < temp)
			{
				temp -= this_player->armor;
				this_player->armor = 0;

				if (this_player->is_alive && !youAreCheating)
				{
					if (endlessMode && endlessConsumeRevive((uint)(this_player - &player[0])))
					{
						// A revive restores armor and stuns enemy fire. Clear the current volley,
						// show each bullet popping, and grant brief invulnerability.
						this_player->invulnerable_ticks = 100;
						for (int es = 0; es < ENEMY_SHOT_MAX; ++es)
						{
							if (!enemyShotAvail[es])
								JE_setupExplosion(enemyShot[es].sx, enemyShot[es].sy, 0, 0, false, false);
							enemyShotAvail[es] = 1;
						}
						soundQueue[3] = S_POWERUP;
					}
					else
					{
						if (!timedBattleMode)
							levelTimer = false;
						this_player->is_alive = false;
						this_player->exploding_ticks = 60;
						// A co-op ship that goes down spectates until the zone ends; the outpost the
						// survivor reaches puts it back in the air. Both down is an ordinary death.
						if (coopEndlessMode)
							endlessPlayerDowned[this_player - &player[0]] = true;
						levelEnd = 40;
						tempVolume = tyrMusicVolume;
						soundQueue[1] = S_EXPLOSION_22;
					}
				}
			}
			else
			{
				this_player->armor -= temp;
				soundQueue[7] = S_HULL_HIT;
			}
		}
	}
	else
	{
		this_player->shield -= temp;

		// Shield-hit explosions follow the hull's local presentation style.
		explosionOpacity = netStyleForSeat((this_player == &player[1]) ? 1u : 0u).opacity;

		shield_hit_ring(this_player);

		explosionOpacity = NET_STYLE_SOLID;
	}

	// Arm presentation-only gauge flashes only on the live pass; rollback must not restart them.
	const int gi = (this_player == &player[1]) ? 1 : 0;
	if (!rollback_resim && gaugeFlashShield && this_player->shield < oldShield)
		shieldGaugeFlash[gi] = GAUGE_FLASH_START;
	if (!rollback_resim && gaugeFlashArmor && this_player->armor < oldArmor)
		armorGaugeFlash[gi] = GAUGE_FLASH_START;

	/* Kinetic Converter's generator share needs shield absorption; its other effects need any hit. */
	if (endlessFxShip(this_player) && (this_player->shield < oldShield || cmHullHit))
	{
		// Re-cap at the generator ceiling: the tick's own recharge/cap already ran.
		// shields[].tpwr is the shield's per-point charge cost, the natural power<->shield rate.
		if (this_player->shield < oldShield)
		{
			const int gained = endlessPerkKineticPower(oldShield - this_player->shield,
			                                           shields[this_player->items.shield].tpwr);
			if (gained > 0)
				endlessGeneratorSet(this_player, endlessGeneratorGet(this_player) + gained);
		}

		endlessKineticCoolSpecial(this_player);

		// Rounds are stateful: this is the one call per hit, so its answer has to be spent here.
		endlessKineticFeedSidekicks(this_player, endlessPerkKineticAmmoRounds(),
		                            endlessPerkKineticChargeStages());
	}

	if (endlessFxShip(this_player) && cmHullHit)
		endlessCountermeasureBurst(this_player);

	// Failsafe extends hull-hit invulnerability without shortening a longer revive/respawn window.
	if (endlessFxShip(this_player) && cmHullHit && this_player->is_alive)
	{
		const uint failsafe = (uint)endlessPerkFailsafeTicks();
		if (this_player->invulnerable_ticks < failsafe)
			this_player->invulnerable_ticks = failsafe;
	}

	JE_wipeShieldArmorBars();
	VGAScreen = VGAScreenSeg; /* side-effect of game_screen */
	JE_drawShield();
	JE_drawArmor();
	VGAScreen = game_screen; /* side-effect of game_screen */

	if (endlessFxShip(this_player))
		endlessRegenHitTaken();

	// Static Discharge uses the actual shield and armor loss because the return value is zero for a
	// fully absorbed hit. Drain only the affected ship's available generator reserve.
	if (endlessFxShip(this_player))
	{
		int lost = 0;
		if (this_player->shield < oldShield) lost += oldShield - this_player->shield;
		if (this_player->armor  < oldArmor)  lost += oldArmor  - this_player->armor;
		if (lost > 0)
		{
			const int drain = (int)endlessStaticDischargeDrain((unsigned)lost);
			endlessGeneratorSet(this_player, endlessGeneratorGet(this_player) - drain);
		}
	}

	return playerDamage;
}

/* How many firing patterns one ship's rear bay offers. */
JE_word JE_portConfigs(const Player *this_player)
{
	return tempW = weaponPort[this_player->items.weapon[REAR_WEAPON].id].opnum;
}
