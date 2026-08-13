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
#include "varz.h"

#include <stdlib.h>  // _Exit (Switch clean-exit path in JE_tyrianHalt)
#include <string.h>  // memset (JE_resetSP)

#include "config.h"
#include "crashlog.h"
#include "destruct.h"
#include "editship.h"
#include "endless.h"
#include "episodes.h"
#include "fonthand.h"
#include "joystick.h"
#include "lds_play.h"
#include "loudness.h"
#include "mainint.h"
#include "mouse.h"
#include "mtrand.h"
#include "network.h"
#include "nortsong.h"
#include "nortvars.h"
#include "opentyr.h"
#include "render_list.h"
#include "rollback.h"
#include "shots.h"
#include "sprite.h"
#include "tyrian2.h"
#include "vga256d.h"
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

/* Demo Stuff */
bool play_demo = false, record_demo = false, stopped_demo = false;
Uint8 demo_num = 0;
FILE *demo_file = NULL;

Uint8 demo_keys;
Uint16 demo_keys_wait;

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
JE_word enemyParkedAbove;   // of enemyOnScreen: parked above the screen with no way to ever enter it (map-stop watchdog, tyrian2.c)
JE_word mapStopStallTicks;  // ticks a scripted map stop has been held only by parked-above enemies
JE_word superEnemy254Jump;

/*EnemyShotData*/
JE_boolean fireButtonHeld;
JE_boolean enemyShotAvail[ENEMY_SHOT_MAX]; /* [1..Enemyshotmax] */
EnemyShotType enemyShot[ENEMY_SHOT_MAX]; /* [1..Enemyshotmax]  */

/* Player Shot Data */
JE_byte     zinglonDuration;

/* Per-ship Soul of Zinglon render request. The display pass places the pillar at
 * the render-rate ship position; an explicit flag distinguishes spent duration 1. */
bool        zinglonPillarActive[2] = { false, false };
int         zinglonPillarCX[2] = { 0, 0 };    /* pillar centre x in game_screen coords */
int         zinglonPillarTemp[2] = { 0, 0 };  /* pillar half-width */

JE_byte     astralDuration;
JE_word     flareDuration;
JE_boolean  flareStart;
JE_shortint flareColChg;
/* The full-screen grade on screen right now was installed by a flare special, not by the level.
   Presentation-only: it lets the Special Tint setting suppress the flare's wash without touching
   levelFilter itself, which is simulation state a peer would desync against. */
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
JE_byte     noclipMode = NOCLIP_OFF;     /* debug: pass through enemies (see enum in varz.h) */
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

// The debug-sync block carries these across the wire in a fixed number of slots, and its loops
// simply stop when they run out; a setting added past the end would go unsynced in silence.
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

/*ExplosionData*/
Explosion explosions[MAX_EXPLOSIONS]; /* [1..ExplosionMax] */
JE_integer explosionFollowAmountX, explosionFollowAmountY;
Uint8 explosionFilter;  /* see varz.h */

/*Repeating Explosions*/
rep_explosion_type rep_explosions[MAX_REPEATING_EXPLOSIONS]; /* [1..20] */

/*SuperPixels*/
superpixel_type superpixels[MAX_SUPERPIXELS]; /* [0..MaxSP] */
static unsigned int last_superpixel;           // shared cursor, exactly as when one window existed
static unsigned int last_uncapped_superpixel;  // write cursor for the high window

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

/* Endless run-persistent hull: the outpost Reinforce tier plus the Ablative Plating perk. The perk
 * half can be NEGATIVE (Glass Cannon), so the result is clamped at both ends. Reinforce is
 * endlessMode-only: a campaign running the effect layer under Debug Mode has no shop to buy it at. */
static void endlessApplyHullBonus(uint p)
{
	if (!endlessFxActive())
		return;

	// Personal perks: the plating (or Glass Cannon drawback) on this hull is this ship's own.
	const uint fxSaved = endlessFxPlayer();
	endlessSetFxPlayer(p);
	int a = (int)player[p].armor + endlessPerkArmorBonus();
	endlessSetFxPlayer(fxSaved);
	if (endlessMode)
		a += endlessArmorBonus[p];
	player[p].armor = (a < 1) ? 1 : (a > 250 ? 250 : a);  // byte-safe, so no JE_byte armor path wraps
}

void JE_getShipInfo(void)
{
	JE_boolean extraShip, extraShip2;

	// An extra ship (id above 90) is described by extraShips[]; ships[] holds only
	// SHIP_NUM+1 entries, so indexing it with such an id reads well past the end. Default those to
	// the standard sheet here; JE_SGr picks the real one for them a few lines down.
	shipGrPtr = (player[0].items.ship <= SHIP_NUM && ships[player[0].items.ship].shipgraphic > 500)
	          ? &spriteSheetT2000 : &spriteSheet9;
	shipGr2ptr = &spriteSheet9;

	powerAdd  = powerSys[player[0].items.generator].power;
	for (uint i = 0; i < COUNTOF(player); ++i)
		player[i].generator_power_add = powerSys[player[i].items.generator].power;

	extraShip = player[0].items.ship > 90;
	if (extraShip)
	{
		JE_byte base = (player[0].items.ship - 91) * 15;
		shipGr = JE_SGr(player[0].items.ship - 90, &shipGrPtr);
		player[0].armor = extraShips[base + 7];
	}
	else
	{
		// Only ids above 90 are extra ships. IDs 19 through 90 fall beyond ships[], so use
		// entry 0 if an edited ship or older save carries a stray id.
		const uint shipIdx = (player[0].items.ship <= SHIP_NUM) ? player[0].items.ship : 0;
		shipGr = ships[shipIdx].shipgraphic - (shipGrPtr == &spriteSheetT2000 ? 500 : 0);
		player[0].armor = ships[shipIdx].dmg;
	}

	endlessApplyHullBonus(0);

	extraShip2 = player[1].items.ship > 90;
	if (extraShip2)
	{
		JE_byte base2 = (player[1].items.ship - 91) * 15;
		shipGr2 = JE_SGr(player[1].items.ship - 90, &shipGr2ptr);
		player[1].armor = extraShips[base2 + 7]; /* bug? */
	}
	else if (dual_ship_mode())
	{
		const uint shipIdx = (player[1].items.ship <= SHIP_NUM) ? player[1].items.ship : 0;
		shipGr2ptr = (ships[shipIdx].shipgraphic > 500) ? &spriteSheetT2000 : &spriteSheet9;
		shipGr2 = ships[shipIdx].shipgraphic - (shipGr2ptr == &spriteSheetT2000 ? 500 : 0);
		player[1].armor = ships[shipIdx].dmg;
	}
	else
	{
		shipGr2 = 0;
		player[1].armor = 10;
	}

	if (coopEndlessMode)
		endlessApplyHullBonus(1);

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		// Arcade lives scaling: the hull is only the 1-life figure, so keep it and raise the real
		// ceiling on top of it. Every caller treats the result as a full hull restore;
		// the between-level outpost is one of them, so armor follows the new ceiling.
		player[i].hull_armor = player[i].armor;
		player[i].initial_armor = arcade_armor_max(&player[i]);
		player[i].armor = player[i].initial_armor;

		// ships[] stops at SHIP_NUM while an "extra" ship is only id > 90, so an id in between
		// would read past the end here too (see the shipGr fallback above).
		const uint shipIdx = (player[i].items.ship <= SHIP_NUM) ? player[i].items.ship : 0;
		uint temp = ((i == 0 && extraShip) ||
		             (i == 1 && extraShip2)) ? 2 : ships[shipIdx].ani;

		if (temp == 0)
		{
			player[i].shot_hit_area_x = 12;
			player[i].shot_hit_area_y = 10;
		}
		else
		{
			player[i].shot_hit_area_x = 11;
			player[i].shot_hit_area_y = 14;
		}
	}
}

JE_word JE_SGr(JE_word ship, Sprite2_array **ptr)
{
	const JE_word GR[15] /* [1..15] */ = {233, 157, 195, 271, 81, 0, 119, 5, 43, 81, 119, 157, 195, 233, 271};

	JE_word tempW = extraShips[(ship - 1) * 15];
	if (tempW > 7)
		*ptr = extraShapes;

	return GR[tempW-1];
}

void JE_resetPlayerOptions(Player *this_player)
{
	// Personal perks: Ordnance Reserves and Rapid Recharge size this ship's own magazines.
	const uint fxSaved = endlessFxPlayer();
	endlessSetFxPlayer((uint)(this_player - player));

	for (uint i = 0; i < COUNTOF(this_player->sidekick); ++i)
	{
		JE_OptionType *this_option = &options[this_player->items.sidekick[i]];

		// Ordnance Reserves perk grows the magazine; the refill cadence is scaled to match, so the
		// deeper reserve still fills in the shipped time instead of trickling in proportionally
		// slower. Both stay keyed to the SHIPPED size, which also keeps the stock `105 - ammo`
		// formula from going negative once a boosted magazine passes 105 rounds.
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

bool hud_sidekicks_dirty = false;

uint hud_sidekick_player_index(void)
{
	// The strip has room for one ship's pods. A dual-ship game simulates both, so it shows
	// the local one; the split linked pair gives the pods to player two; otherwise player one.
	return dual_ship_mode() ? gameplay_local_player_index()
	                        : (split_arcade_mode() ? 1u : 0u);
}

int hud_sidekick_ammo_y(uint slot)
{
	return hud_sidekick_y[split_arcade_mode() ? 1 : 0][slot] + 13;
}

// Draw-only companion to JE_drawOptions: repaints the sidekick HUD boxes from current state.
void JE_drawOptionsHUD(void)
{
	// A silent re-simulation pass suppresses sprite blits but still runs plain fills, so
	// painting here would clear the box and never put the icon back. Leave the strip alone and
	// let the level loop settle it on the next presented frame.
	if (rollback_resim_silent)
	{
		hud_sidekicks_dirty = true;
		return;
	}

	Player *this_player = &player[hud_sidekick_player_index()];

	for (uint i = 0; i < COUNTOF(this_player->sidekick); ++i)
	{
		JE_OptionType *this_option = &options[this_player->items.sidekick[i]];

		const int y = hud_sidekick_y[split_arcade_mode() ? 1 : 0][i];

		const int hud_x = HUD_X(284);
		fill_rectangle_xy(VGAScreenSeg, hud_x, y, hud_x + 28, y + 15, 0);
		if (this_option->icongr > 0)
			blit_sprite(VGAScreenSeg, hud_x, y, OPTION_SHAPES, this_option->icongr - 1);  // sidekick HUD icon
		draw_segmented_gauge(VGAScreenSeg, hud_x, y + 13, 112, 2, 2, AMMO_GAUGE_STEP(this_player->sidekick[i].ammo_max), this_player->sidekick[i].ammo);
	}
}

void JE_drawOptionLevel(void)
{
	if (split_arcade_mode())
	{
		for (temp = 1; temp <= 3; temp++)
		{
			fill_rectangle_xy(VGAScreenSeg, HUD_X(268), 127 + (temp - 1) * 6, HUD_X(269), 127 + 3 + (temp - 1) * 6, 193 + ((player[1].items.sidekick_level - 100) == temp) * 11);
		}
	}
}

void JE_tyrianShutdown(bool saveConfiguration)
{
#ifdef WITH_NETWORK
	network_shutdown();
#endif
	deinit_audio();
	destruct_deinit();
	tyrian2_deinit();
	rl_deinit();
	deinit_video();
	deinit_joysticks();

	free_main_shape_tables();
	free_sprite2s(&shopSpriteSheet);
	free_sprite2s(&explosionSpriteSheet);

	for (int i = 0; i < SOUND_COUNT; ++i)
	{
		free(soundSamples[i]);
		soundSamples[i] = NULL;
	}

	if (demo_file != NULL)
	{
		fclose(demo_file);
		demo_file = NULL;
	}

	if (saveConfiguration)
		JE_saveConfiguration();

	music_deinit();
	JE_freeExtraShapes();
	rollback_deinit();
	config_deinit(&opentyrian_config);
#ifdef WITH_NETWORK
	network_deinit();
#endif
}

void JE_tyrianHalt(JE_byte code)
{
	// Code 1 is an unrecoverable data error. Write its report before exit()
	// bypasses the crash handlers; duplicate reports are ignored.
	if (code == 1)
		crashlog_report_fatal("FATAL (JE_tyrianHalt error exit)",
		                      "JE_tyrianHalt(1) -- unrecoverable data/level error; see phase + stack");

	JE_tyrianShutdown(code != 9);

	/* endkeyboard; */

	if (code == 9)
	{
	}

	if (code == 5)
	{
		code = 0;
	}

	if (trentWin)
	{
		printf("\n"
		       "\n"
		       "\n"
		       "\n"
		       "Sleep well, Trent, you deserve the rest.\n"
		       "You now have permission to borrow my ship on your next mission.\n"
		       "\n"
		       "Also, you might want to try out the YESXMAS parameter in Dos.\n"
		       "  Type: File0001 YESXMAS\n"
		       "\n"
		       " Press a Key to Quit\n"
		       "\n");
	}

	SDL_Quit();

#ifdef __SWITCH__
	// Switch: libc exit()'s atexit/stdio teardown NULL-derefs in newlib once romfs is
	// gone; everything is already flushed, so _Exit and skip it.
	_Exit(code);
#else
	exit(code);
#endif
}

// Opening Salvo: the specials that spawn no shot (repulsor, attractor, invuln, repair) have no
// bullet to trail sparks off, so a boosted one would look identical to a plain one. Burst off the
// ship instead, in the gauge's green. No-op outside a window.
static void salvo_special_burst(JE_byte playerNum)
{
	if (!endlessOpeningSalvoVolleyActive())
		return;

	const Player *const p = &player[playerNum - 1];
	JE_doSP(p->x + 7, p->y + 10, 24, 11, ENDLESS_SALVO_SPARK_COLOR, false);
}

/* The pointer that aims this ship's special. Ship two has one of its own only where the ships are
 * independent; the linked pair shares a single arsenal, and with it a single aim. */
static int special_mouse_x_for(JE_byte playerNum) { return dual_ship_mode() && playerNum == 2 ? mouseXB : mouseX; }
static int special_mouse_y_for(JE_byte playerNum) { return dual_ship_mode() && playerNum == 2 ? mouseYB : mouseY; }

/* A special went off during the JE_doSpecialShot call in progress. Set here rather than at the four
   gates that call this, so a fifth can never forget to; JE_doSpecialShot clears it on entry and
   reads it at the end, which keeps it scoped to one ship's turn through the tick. */
static bool specialFiredThisCall = false;

void JE_specialComplete(JE_byte playerNum, JE_byte specialType)
{
	Player *const this_player = dual_ship_mode() ? &player[playerNum - 1] : &player[0];
	const int special_mouse_x = special_mouse_x_for(playerNum);
	const int special_mouse_y = special_mouse_y_for(playerNum);

	specialFiredThisCall = true;
	nextSpecialWait = 0;
	switch (special[specialType].stype)
	{
		/*Weapon*/
		case 1:
			if (playerNum == 1)
				b = player_shot_create(0, SHOT_SPECIAL2, player[0].x, player[0].y, special_mouse_x, special_mouse_y, special[specialType].wpn, playerNum);
			else
				b = player_shot_create(0, SHOT_SPECIAL2, player[1].x, player[1].y, special_mouse_x, special_mouse_y, special[specialType].wpn, playerNum);

			shotRepeat[SHOT_SPECIAL] = shotRepeat[SHOT_SPECIAL2];
			break;
		/*Repulsor*/
		case 2:
		{
			const int push = endlessOpeningSalvoScale(1);  // Opening Salvo: shoves that much harder
			salvo_special_burst(playerNum);

			// Local int counter, not the global JE_byte `temp`: ENEMY_SHOT_MAX is 500, which a byte
			// counter can never reach (it wraps at 255), so `temp` here would loop forever and hang
			// the moment the Repulsor fires. (The pool grew past 255 for endless; see ENEMY_SHOT_MAX.)
			for (int es = 0; es < ENEMY_SHOT_MAX; es++)
			{
				if (!enemyShotAvail[es])
				{
					if (this_player->x > enemyShot[es].sx)
						enemyShot[es].sxm -= push;
					else if (this_player->x < enemyShot[es].sx)
						enemyShot[es].sxm += push;

					if (this_player->y > enemyShot[es].sy)
						enemyShot[es].sym -= push;
					else if (this_player->y < enemyShot[es].sy)
						enemyShot[es].sym += push;
				}
			}
			break;
		}
		/*Zinglon Blast*/
		case 3:
			zinglonDuration = 50;
			shotRepeat[SHOT_SPECIAL] = 100;
			soundQueue[7] = S_SOUL_OF_ZINGLON;
			break;
		/*Attractor*/
		case 4:
		{
			// Opening Salvo: hauls that much harder. exc/eyc are Sint8 and accumulate per firing,
			// so clamp below; a wrapped speed would fling the pickup the wrong way.
			const int pull = endlessOpeningSalvoScale(1);
			salvo_special_burst(playerNum);
			for (temp = 0; temp < 100; temp++)
			{
				if (enemyAvail[temp] != 1 && enemy[temp].scoreitem &&
				    enemy[temp].evalue != 0)
				{
					int exc = enemy[temp].exc, eyc = enemy[temp].eyc;

					if (this_player->x > enemy[temp].ex)
						exc += pull;
					else if (this_player->x < enemy[temp].ex)
						exc -= pull;

					if (this_player->y > enemy[temp].ey)
						eyc += pull;
					else if (this_player->y < enemy[temp].ey)
						eyc -= pull;

					enemy[temp].exc = (JE_shortint)(exc > 120 ? 120 : (exc < -120 ? -120 : exc));
					enemy[temp].eyc = (JE_shortint)(eyc > 120 ? 120 : (eyc < -120 ? -120 : eyc));
				}
			}
			break;
		}
		/*Flare*/
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
		case 16:
			if (flareDuration == 0)
				flareStart = true;

			specialWeaponWpn = special[specialType].wpn;
			linkToPlayer = false;
			spraySpecial = false;
			switch (special[specialType].stype)
			{
				case 5:
					specialWeaponFilter = 7;
					specialWeaponFreq = 2;
					flareDuration = endlessPerkSpecialDuration(50, 0);
					break;
				case 6:
					specialWeaponFilter = 1;
					specialWeaponFreq = 7;
					flareDuration = endlessPerkSpecialDuration(200 + 25 * this_player->items.weapon[FRONT_WEAPON].power, 0);
					break;
				case 7:
					specialWeaponFilter = 3;
					specialWeaponFreq = 3;
					flareDuration = endlessPerkSpecialDuration(50 + 10 * this_player->items.weapon[FRONT_WEAPON].power, 0);
					// zinglonDuration is deliberately NOT stretched: its beam brightness is drawn as
					// `25 - abs(zinglonDuration - 25)`, a ramp that only works on the stock 50 ticks.
					zinglonDuration = 50;
					shotRepeat[SHOT_SPECIAL] = 100;
					soundQueue[7] = S_SOUL_OF_ZINGLON;
					break;
				case 8:
					specialWeaponFilter = -99;
					specialWeaponFreq = 7;
					flareDuration = endlessPerkSpecialDuration(10 + this_player->items.weapon[FRONT_WEAPON].power, 0);
					break;
				case 9:
					specialWeaponFilter = -99;
					specialWeaponFreq = 8;
					flareDuration = endlessPerkSpecialDuration(8 + 2 * this_player->items.weapon[FRONT_WEAPON].power, 0);
					linkToPlayer = true;
					nextSpecialWait = special[specialType].pwr;
					break;
				case 10:
					specialWeaponFilter = -99;
					specialWeaponFreq = 8;
					flareDuration = endlessPerkSpecialDuration(14 + 4 * this_player->items.weapon[FRONT_WEAPON].power, 0);
					linkToPlayer = true;
					break;
				case 11:
					specialWeaponFilter = -99;
					specialWeaponFreq = special[specialType].pwr;
					flareDuration = endlessPerkSpecialDuration(10 + 10 * this_player->items.weapon[FRONT_WEAPON].power, 0);
					astralDuration = endlessPerkSpecialDuration(20 + 10 * this_player->items.weapon[FRONT_WEAPON].power, 255);
					break;
				case 16:
					specialWeaponFilter = -99;
					specialWeaponFreq = 8;
					flareDuration = endlessPerkSpecialDuration(temp2 * 16 + 8, 0);
					linkToPlayer = true;
					spraySpecial = true;
					break;
			}
			break;
		case 12:
			// Opening Salvo x2.5, on top of Ordnance Reserves' stretch. invulnerable_ticks is a uint.
			player[playerNum-1].invulnerable_ticks = endlessOpeningSalvoScale(endlessPerkSpecialDuration(temp2 * 10, 0));
			salvo_special_burst(playerNum);

			// A Super Arcade ship's own invulnerability burst, so it belongs to the firing ship.
			if (player_sa_ship(this_player) != SA_NONE)
			{
				shotRepeat[SHOT_SPECIAL] = 250;
				b = player_shot_create(0, SHOT_SPECIAL2, this_player->x, this_player->y,
				                       special_mouse_x, special_mouse_y, 707, playerNum);
				this_player->invulnerable_ticks = 100;
			}
			break;
		// Repair specials, Opening Salvo x2.5. Vanilla leans on JE_drawArmor's blanket 28 clamp to
		// bound these, but endless deliberately SKIPS it (reinforced hulls exceed 28), so cap on the
		// hull's own max; the rule an armour PICKUP follows. Endless-only; vanilla is untouched.
		case 13:
			this_player->armor += endlessOpeningSalvoScale(temp2 / 4 + 1);
			if (endlessFxActive() && this_player->initial_armor > 0 && this_player->armor > this_player->initial_armor)
				this_player->armor = this_player->initial_armor;
			salvo_special_burst(playerNum);

			soundQueue[3] = S_POWERUP;
			break;
		case 14:
		{
			// Vanilla's repair-the-OTHER-hull special, and co-op keeps that meaning: it heals the
			// partner. Aiming it at the firer instead made it a second copy of case 13. The linked
			// pair has no partner ship of its own, so there it stays on hull two.
			const JE_byte repair_num = dual_ship_mode() ? (JE_byte)(3 - playerNum) : 2;
			Player *const repair_player = &player[repair_num - 1];
			repair_player->armor += endlessOpeningSalvoScale(temp2 / 4 + 1);
			if (endlessFxActive() && repair_player->initial_armor > 0 && repair_player->armor > repair_player->initial_armor)
				repair_player->armor = repair_player->initial_armor;
			salvo_special_burst(repair_num);

			soundQueue[3] = S_POWERUP;
			break;
		}

		case 17:  // spawn left or right sidekick
			soundQueue[3] = S_POWERUP;

			if (this_player->items.sidekick[LEFT_SIDEKICK] == special[specialType].wpn)
			{
				this_player->items.sidekick[RIGHT_SIDEKICK] = special[specialType].wpn;
				shotMultiPos[RIGHT_SIDEKICK] = 0;
			}
			else
			{
				this_player->items.sidekick[LEFT_SIDEKICK] = special[specialType].wpn;
				shotMultiPos[LEFT_SIDEKICK] = 0;
			}

			if (dual_ship_mode())
			{
				JE_resetPlayerOptions(this_player);
				JE_drawOptionsHUD();
			}
			else
			{
				JE_drawOptions();
			}
			break;

		case 18:  // spawn right sidekick
			this_player->items.sidekick[RIGHT_SIDEKICK] = special[specialType].wpn;

			if (dual_ship_mode())
			{
				JE_resetPlayerOptions(this_player);
				JE_drawOptionsHUD();
			}
			else
			{
				JE_drawOptions();
			}

			soundQueue[4] = S_POWERUP;

			shotMultiPos[RIGHT_SIDEKICK] = 0;
			break;
	}
}

// Flare-type specials hold flareDuration > 0 while active; instant specials don't.
// Used to decide whether the equipped special can fire alongside a twiddle flare.
static bool special_is_flare(JE_byte sidx)
{
	const JE_byte st = special[sidx].stype;
	return (st >= 5 && st <= 11) || st == 16;
}

// Debug twiddle autofire has separate flare ownership and pacing. File scope exposes it to rollback.
static JE_boolean flareFromTwiddle = false;
static JE_word twiddleFlareShotWait = 0;
static JE_word twiddleWait = 0;

void JE_doSpecialShot(JE_byte playerNum, uint *armor, uint *shield)
{
	Player *const this_player = dual_ship_mode() ? &player[playerNum - 1] : &player[0];
	const int special_mouse_x = special_mouse_x_for(playerNum);
	const int special_mouse_y = special_mouse_y_for(playerNum);

	const uint special_player = (uint)(this_player - player);
	specialFiredThisCall = false;

	if (shotRepeat[SHOT_SPECIAL] > 0)
	{
		--shotRepeat[SHOT_SPECIAL];
	}
	if (specialWait > 0)
	{
		specialWait--;
	}

	// Sample readiness before either fire gate can spend it. The HUD uses this sample to show a
	// special that becomes ready and fires within the same tick; flareDuration must match the gate.
	const bool special_armed = shotRepeat[SHOT_SPECIAL] == 0 && specialWait == 0
	                        && flareDuration == 0 && zinglonDuration < 2;

	temp = SFExecuted[playerNum-1];
	if (temp > 0 && shotRepeat[SHOT_SPECIAL] == 0 && flareDuration == 0)
	{
		temp2 = special[temp].pwr;

		bool can_afford = true;

		if (temp2 > 0)
		{
			if (temp2 < 98)  // costs some shield
			{
				if (*shield >= temp2)
					*shield -= temp2;
				else
					can_afford = false;
			}
			else if (temp2 == 98)  // costs all shield
			{
				if (*shield < 4)
					can_afford = false;
				temp2 = *shield;
				*shield = 0;
			}
			else if (temp2 == 99)  // costs half shield
			{
				temp2 = *shield / 2;
				*shield = temp2;
			}
			else  // costs some armor
			{
				temp2 -= 100;
				if (*armor > temp2)
					*armor -= temp2;
				else
					can_afford = false;
			}
		}

		shotMultiPos[SHOT_SPECIAL] = 0;
		shotMultiPos[SHOT_SPECIAL2] = 0;

		if (can_afford)
			JE_specialComplete(playerNum, temp);

		SFExecuted[playerNum-1] = 0;

		JE_wipeShieldArmorBars();
		VGAScreen = VGAScreenSeg; /* side-effect of game_screen */
		JE_drawShield();
		JE_drawArmor();
		VGAScreen = game_screen; /* side-effect of game_screen */
	}

	if ((playerNum == 1 || dual_ship_mode()) && this_player->items.special > 0)
	{  /*Main Begin*/

		if (superArcadeMode > 0 && (button[2-1] || button[3-1]))
		{
			fireButtonHeld = false;
		}
		if (!button[1-1] && !(superArcadeMode != SA_NONE && (button[2-1] || button[3-1])))
		{
			fireButtonHeld = false;
		}
		else if (shotRepeat[SHOT_SPECIAL] == 0 && !fireButtonHeld &&
		         (flareDuration == 0 || (flareFromTwiddle && !special_is_flare(this_player->items.special))) &&
		         specialWait == 0)
		{
			fireButtonHeld = true;
			JE_specialComplete(playerNum, this_player->items.special);
		}
		else if (endlessMode)
		{
			// Endless latches held input during recharge so it cannot fire on the ready
			// edge. Autofire Special bypasses this latch below.
			fireButtonHeld = true;
		}

	}  /*Main End*/

	if ((autoFireSpecial || endlessPerkAutoFireSpecial()) && (playerNum == 1 || dual_ship_mode()) && this_player->items.special > 0 &&
		shotRepeat[SHOT_SPECIAL] == 0 && specialWait == 0 &&
		(flareDuration == 0 || (flareFromTwiddle && !special_is_flare(this_player->items.special))) &&
		(button[0] || (superArcadeMode != SA_NONE && (button[1] || button[2]))))
	{
		JE_specialComplete(playerNum, this_player->items.special);
	}

	// Debug: force-fire the selected twiddle's special. Runs after the equipped
	// special (which keeps priority) on its own cooldown (twiddleWait), ignores the
	// shield/armor cost, and won't stack onto an active flare.
	{
		if (playerNum == 1)
		{
			if (twiddleWait > 0)
				--twiddleWait;

			const bool want = debugTwiddleTrigger
			               || (debugAutofireTwiddle && button[0] && twiddleWait == 0);

			if (want && debugTwiddleSpecial >= 1 && debugTwiddleSpecial <= SPECIAL_NUM &&
			    flareDuration == 0)
			{
				debugTwiddleTrigger = false;

				// JE_specialComplete reads global temp2 for its duration/effect maths;
				// seed it like the cost path but without deducting, so it fires free.
				temp2 = special[debugTwiddleSpecial].pwr;
				if (temp2 == 98)        temp2 = *shield;
				else if (temp2 == 99)   temp2 = *shield / 2;
				else if (temp2 >= 100)  temp2 -= 100;
				shotMultiPos[SHOT_SPECIAL] = 0;
				shotMultiPos[SHOT_SPECIAL2] = 0;

				const int savedSR = shotRepeat[SHOT_SPECIAL];
				JE_specialComplete(playerNum, debugTwiddleSpecial);
				if (flareDuration > 0)
				{
					// Flare twiddle: tag the flare as the twiddle's (equipped special keeps
					// firing) and let the flare's own duration pace the re-fire.
					flareFromTwiddle = true;
					twiddleFlareShotWait = 0;
					twiddleWait = 0;
				}
				else
				{
					twiddleWait = 14;  // instant twiddle: small cadence so it fires right off cooldown
				}
				// Don't let the twiddle's special disturb the EQUIPPED special's cooldown
				// (the twiddle paces itself via twiddleWait / twiddleFlareShotWait).
				shotRepeat[SHOT_SPECIAL] = savedSR;
			}
		}
	}

	if (astralDuration > 0)
		astralDuration--;

	shotAvail[MAX_PWEAPON-1] = 0;
	if (flareDuration > 1)
	{
		if (specialWeaponFilter != -99)
		{
			if (levelFilter == -99 && levelBrightness == -99)
			{
				filterActive = false;
			}
			if (!filterActive)
			{
				levelFilter = specialWeaponFilter;
				if (levelFilter == 7)
				{
					levelBrightness = 0;
				}
				filterActive = true;
				flareOwnsFilter = true;
			}

			if (mt_rand() % 2 == 0)
				flareColChg = -1;
			else
				flareColChg = 1;

			if (levelFilter == 7)
			{
				if (levelBrightness < -6)
				{
					flareColChg = 1;
				}
				if (levelBrightness > 6)
				{
					flareColChg = -1;
				}
				levelBrightness += flareColChg;
			}
		}

		if (flareFromTwiddle && twiddleFlareShotWait > 0)
			--twiddleFlareShotWait;

		if ((signed)(mt_rand() % 6) < specialWeaponFreq)
		{
			b = MAX_PWEAPON;

			if (linkToPlayer)
			{
				if (flareFromTwiddle)
				{
					// Twiddle flare: pace mines on twiddleFlareShotWait, keeping the
					// equipped special's shotRepeat[SHOT_SPECIAL] untouched.
					if (twiddleFlareShotWait == 0)
					{
						const int savedSR = shotRepeat[SHOT_SPECIAL];
						b = player_shot_create(0, SHOT_SPECIAL, this_player->x, this_player->y, special_mouse_x, special_mouse_y, specialWeaponWpn, playerNum);
						twiddleFlareShotWait = shotRepeat[SHOT_SPECIAL];  // capture the mine cadence
						shotRepeat[SHOT_SPECIAL] = savedSR;
					}
				}
				else if (shotRepeat[SHOT_SPECIAL] == 0)
				{
					b = player_shot_create(0, SHOT_SPECIAL, this_player->x, this_player->y, special_mouse_x, special_mouse_y, specialWeaponWpn, playerNum);
				}
			}
			else
			{
				// Scatter across the full widescreen playfield. Sequence RNG draws so compilers cannot
				// assign them to opposite coordinates.
				const int scatter_x = PLAYFIELD_LEFT + mt_rand() % PLAYFIELD_WIDTH;
				const int scatter_y = mt_rand() % 184;
				b = player_shot_create(0, SHOT_SPECIAL, scatter_x, scatter_y, special_mouse_x, special_mouse_y, specialWeaponWpn, playerNum);
			}

			if (spraySpecial && b != MAX_PWEAPON)
			{
				playerShotData[b].shotXM = (mt_rand() % 5) - 2;
				playerShotData[b].shotYM = (mt_rand() % 5) - 2;
				if (playerShotData[b].shotYM == 0)
				{
					playerShotData[b].shotYM++;
				}
			}
		}

		flareDuration--;
		if (flareDuration == 1)
		{
			specialWait = nextSpecialWait;
		}
	}
	else if (flareStart)
	{
		flareStart = false;
		if (!flareFromTwiddle)  // twiddle flare paces itself; leave the equipped cooldown alone
			shotRepeat[SHOT_SPECIAL] = linkToPlayer ? 15 : 200;
		flareDuration = 0;
		flareFromTwiddle = false;
		twiddleFlareShotWait = 0;
		if (levelFilter == specialWeaponFilter)
		{
			levelFilter = -99;
			levelBrightness = -99;
			filterActive = false;
			flareOwnsFilter = false;
		}
	}

	if (zinglonDuration > 1)
	{
		temp = 25 - abs(zinglonDuration - 25);

		// Record the pillar for the render layer (JE_starShowVGA) instead of drawing:
		// into game_screen it would snap at 35Hz and freeze the scrolled background.
		zinglonPillarActive[special_player] = true;
		zinglonPillarCX[special_player] = this_player->x + 7;
		zinglonPillarTemp[special_player] = temp;

		// Opening Salvo: the pillar is a brightness effect, not a sprite, so it has no colour to
		// trail. Scatter sparks up the beam in the salvo's green, width following its own ramp.
		if (endlessOpeningSalvoVolleyActive() && temp > 0)
			JE_doSP(this_player->x + 7, mt_rand() % 184, 6, (JE_byte)temp, ENDLESS_SALVO_SPARK_COLOR, false);

		zinglonDuration--;
		if (zinglonDuration % 5 == 0)
		{
			shotAvail[MAX_PWEAPON-1] = 1;
		}
	}

	// Publish the displayed ship's clocks after all fire and duration updates. `flareStart` keeps
	// the final burn tick connected to the recharge that it installs.
	if (hud_special_block_shown(special_player))
		hud_special_light_publish(MAX(shotRepeat[SHOT_SPECIAL], specialWait),
		                          MAX(flareStart ? (int)flareDuration : 0,
		                              zinglonDuration > 1 ? (int)zinglonDuration : 0),
		                          special_armed, specialFiredThisCall);
}

void JE_setupExplosion(
	JE_integer x,
	JE_integer y,
	JE_integer deltaY,
	JE_integer type,
	bool fixedPosition,  // true when coin/gem value
	bool followPlayer)   // true when player shield (1P only)
{
	const struct {
		JE_word sprite;
		JE_byte ttl;
	} explosion_data[54] /* [1..54] */ = {
		{ 144,  7 },
		{ 120, 12 },
		{ 190, 12 },
		{ 209, 12 },
		{ 152, 12 },
		{ 171, 12 },
		{ 133,  7 },   /*White Smoke*/
		{   1, 12 },
		{  20, 12 },
		{  39, 12 },
		{  58, 12 },
		{ 110,  3 },
		{  76,  7 },
		{  91,  3 },
/*15*/	{ 227,  3 },
		{ 230,  3 },
		{ 233,  3 },
		{ 252,  3 },
		{ 246,  3 },
/*20*/	{ 249,  3 },
		{ 265,  3 },
		{ 268,  3 },
		{ 271,  3 },
		{ 236,  3 },
/*25*/	{ 239,  3 },
		{ 242,  3 },
		{ 261,  3 },
		{ 274,  3 },
		{ 277,  3 },
/*30*/	{ 280,  3 },
		{ 299,  3 },
		{ 284,  3 },
		{ 287,  3 },
		{ 290,  3 },
/*35*/	{ 293,  3 },
		{ 165,  8 },   /*Coin Values*/
		{ 184,  8 },
		{ 203,  8 },
		{ 222,  8 },
		{ 168,  8 },
		{ 187,  8 },
		{ 206,  8 },
		{ 225, 10 },
		{ 169, 10 },
		{ 188, 10 },
		{ 207, 20 },
		{ 226, 14 },
		{ 170, 14 },
		{ 189, 14 },
		{ 208, 14 },
		{ 246, 14 },
		{ 227, 14 },
		{ 265, 14 },
		{  96,  3 }
	};

	if (y > -16 && y < 190)
	{
		for (int i = 0; i < MAX_EXPLOSIONS; i++)
		{
			if (explosions[i].ttl == 0)
			{
				explosions[i].x = x;
				explosions[i].y = y;
				if (type == 6)
				{
					explosions[i].y += 12;
					explosions[i].x += 2;
				}
				else if (type == 98 || type == 198)
				{
					type = 6;
				}
				explosions[i].sprite = explosion_data[type].sprite;
				explosions[i].ttl = explosion_data[type].ttl;
				explosions[i].followPlayer = followPlayer;
				explosions[i].fixedPosition = fixedPosition;
				explosions[i].deltaY = deltaY;
				explosions[i].filter = explosionFilter;
				explosions[i].id_gen++;  // distinct interpolation id for this reuse of the slot
				break;
			}
		}
	}
}

void JE_setupExplosionLarge(JE_boolean enemyGround, JE_byte exploNum, JE_integer x, JE_integer y)
{
	if (y >= 0)
	{
		if (enemyGround)
		{
			JE_setupExplosion(x - 6, y - 14, 0,  2, false, false);
			JE_setupExplosion(x + 6, y - 14, 0,  4, false, false);
			JE_setupExplosion(x - 6, y,      0,  3, false, false);
			JE_setupExplosion(x + 6, y,      0,  5, false, false);
		}
		else
		{
			JE_setupExplosion(x - 6, y - 14, 0,  7, false, false);
			JE_setupExplosion(x + 6, y - 14, 0,  9, false, false);
			JE_setupExplosion(x - 6, y,      0,  8, false, false);
			JE_setupExplosion(x + 6, y,      0, 10, false, false);
		}

		bool big;

		if (exploNum > 10)
		{
			exploNum -= 10;
			big = true;
		}
		else
		{
			big = false;
		}

		if (exploNum)
		{
			for (int i = 0; i < MAX_REPEATING_EXPLOSIONS; i++)
			{
				if (rep_explosions[i].ttl == 0)
				{
					rep_explosions[i].ttl = exploNum;
					rep_explosions[i].delay = 2;
					rep_explosions[i].x = x;
					rep_explosions[i].y = y;
					rep_explosions[i].big = big;
					rep_explosions[i].filter = explosionFilter;
					break;
				}
			}
		}
	}
}

#define GAUGE_FLASH_START 6
int shieldGaugeFlash[2] = { 0, 0 };
int armorGaugeFlash[2]  = { 0, 0 };
static float gaugeFlashAlpha = 1.0f;

/* Where the shield/armor gauges paint. The tick draws take the classic 1x HUD surface; the
 * present pass hands in the supersampled frame instead, so the bars render NxN there rather than
 * arriving block-expanded off VGAScreenSeg. All gauge geometry stays in 1x HUD coordinates and is
 * multiplied on the way to the pixels, so scale 1 reproduces the classic path byte-for-byte. */
static SDL_Surface *gauge_dst = NULL;
static int gauge_scale = 1;

/* Shield/armor levels at the previous and current tick. The bars are event-driven, so without
 * these the present pass could only redraw them at whole tick values and they would step. */
static float gauge_shield_prev[2], gauge_shield_cur[2];
static float gauge_armor_prev[2],  gauge_armor_cur[2];
static bool  gauge_interp = false;   // present pass: draw between the two, not at the live value
static float gaugeLevelAlpha = 1.0f;

static SDL_Surface *gauge_surface(void)
{
	return gauge_dst != NULL ? gauge_dst : VGAScreen;
}

static void gauge_fill(int x1, int y1, int x2, int y2, Uint8 color)
{
	const int s = gauge_scale;
	fill_rectangle_xy(gauge_surface(), x1 * s, y1 * s, (x2 + 1) * s - 1, (y2 + 1) * s - 1, color);
}

static float gauge_level(float prev, float cur, float live)
{
	if (!gauge_interp)
		return live;
	return prev + (cur - prev) * gaugeLevelAlpha;
}

static float gauge_shield_level(uint i)
{
	return gauge_level(gauge_shield_prev[i], gauge_shield_cur[i], (float)player[i].shield);
}

static float gauge_armor_level(uint i)
{
	return gauge_level(gauge_armor_prev[i], gauge_armor_cur[i], (float)player[i].armor);
}

// Seed both endpoints from the live values, so a level start (or a first draw) has nothing to
// interpolate away from.
void JE_resetGaugeRender(void)
{
	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		gauge_shield_prev[i] = gauge_shield_cur[i] = (float)player[i].shield;
		gauge_armor_prev[i]  = gauge_armor_cur[i]  = (float)player[i].armor;
	}
}

static int gauge_flash_render(int cur)
{
	if (cur <= 0)
		return 0;
	int i = (int)(cur + 1.0f - gaugeFlashAlpha + 0.5f);
	if (i < 0)
		i = 0;
	return i;
}

static bool gauge_flash_any(void)
{
	for (int i = 0; i < 2; ++i)
		if (shieldGaugeFlash[i] > 0 || armorGaugeFlash[i] > 0)
			return true;
	return false;
}

// Silent replay defers raw gauge painting and requests one live repaint.
bool hud_bars_dirty = false;

// Draw-only repaint of both gauges from current state onto the HUD surface.
void JE_repaintShieldArmorBars(void)
{
	JE_wipeShieldArmorBars();
	SDL_Surface *saved = VGAScreen;
	VGAScreen = VGAScreenSeg;
	JE_drawShield();
	JE_drawArmor();
	VGAScreen = saved;
}

void JE_updateGaugeFlash(void)
{
	// Latch this tick's levels for the present pass to interpolate between. Held off the replay
	// passes so a rollback cannot shift the pair twice, but `cur` still tracks every pass: a
	// correction that changes the hull must not leave a stale bar on screen for a whole tick.
	if (!rollback_resim)
		for (uint i = 0; i < COUNTOF(player); ++i)
		{
			gauge_shield_prev[i] = gauge_shield_cur[i];
			gauge_armor_prev[i]  = gauge_armor_cur[i];
		}
	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		gauge_shield_cur[i] = (float)player[i].shield;
		gauge_armor_cur[i]  = (float)player[i].armor;
	}

	if (!gauge_flash_any())
		return;

	// One step per REAL tick, which is why the replay passes are excluded: a rollback re-runs ticks
	// that already spent their step, and a deep enough correction burned a whole glow out inside a
	// single displayed frame. The arming above is held off the replay passes for the same reason.
	if (!rollback_resim)
	{
		for (int i = 0; i < 2; ++i)
		{
			if (shieldGaugeFlash[i] > 0)
				--shieldGaugeFlash[i];
			if (armorGaugeFlash[i] > 0)
				--armorGaugeFlash[i];
		}
	}

	gaugeFlashAlpha = 1.0f;
	JE_repaintShieldArmorBars();
}

void gauge_bars_present(SDL_Surface *dst, int scale, float alpha)
{
	gauge_dst = dst;
	gauge_scale = scale;
	gaugeFlashAlpha = alpha;
	gaugeLevelAlpha = alpha;
	gauge_interp = true;

	JE_repaintShieldArmorBars();

	gauge_interp = false;
	gaugeLevelAlpha = 1.0f;
	gaugeFlashAlpha = 1.0f;
	gauge_scale = 1;
	gauge_dst = NULL;
}

// The whole slot, not just the rows above the bar the classic wipe stopped at: the present pass
// draws at an interpolated level that can sit BELOW the tick's, and on the supersampled frame the
// block-expanded 1x bar underneath has to go entirely. Every caller redraws immediately after.
void JE_wipeShieldArmorBars(void)
{
	if (rollback_resim_silent)
	{
		hud_bars_dirty = true;
		return;
	}

	SDL_Surface *const saved = gauge_dst;
	if (gauge_dst == NULL)
		gauge_dst = VGAScreenSeg;  // the wipe never followed VGAScreen; the bar draws do

	for (uint g = 0; g < 2; ++g)
	{
		const int x = (g == 0) ? HUD_X(270) : HUD_X(307);
		if (!split_arcade_mode() || galagaMode)
		{
			gauge_fill(x, 137, x + 8, 194, 0);
		}
		else
		{
			for (uint i = 0; i < COUNTOF(player); ++i)
				gauge_fill(x, 60 + 134 * i - 44, x + 8, 60 + 134 * i, 0);
		}
	}

	gauge_dst = saved;
}

/* Which ships carry the Endless effect layer: both of them in an online co-op run, player 1
 * otherwise (the campaign debug layer only ever equips one). */
static bool endlessFxShip(const Player *this_player)
{
	return endlessFxActive() && (coopEndlessMode || this_player == &player[0]);
}

/* The generator reserve belonging to the ship being hit. In a dual-ship session each ship keeps
 * its own and the global `power` is only the scratch context the weapon code runs in, which is
 * NOT loaded at the damage sites (see coop_ship_runtime_load in mainint.c). */
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

/* Wind back both clocks that gate the ship's special: shotRepeat[SHOT_SPECIAL] is the ordinary
 * cadence, specialWait the one a flare installs on the way out. Same storage split as the
 * generator above. */
static void endlessKineticCoolSpecial(Player *this_player)
{
	JE_byte *const clocks[2] = {
		dual_ship_mode() ? &this_player->shot_repeat[SHOT_SPECIAL] : &shotRepeat[SHOT_SPECIAL],
		dual_ship_mode() ? &this_player->special_wait              : &specialWait,
	};

	for (uint i = 0; i < COUNTOF(clocks); ++i)
		*clocks[i] -= (JE_byte)endlessPerkKineticCooldownCut(*clocks[i]);
}

/* Feed the ship's sidekicks: `rounds` into a magazine, `stages` up a charge ramp. A pod can have
 * both, since the fire path adds a charge stage to an ammo pod's shot. The ammo gauge is
 * event-drawn and needs the dirty flag; a charge stage rides the pod sprite, redrawn every tick. */
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
	// Push the damage past every shield+armor total; a held revive can still catch the lethal blow
	// on the death path below, which keeps the interaction honest rather than an unavoidable game-over.
	if (endlessFxShip(this_player) && (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_NITRO))
		temp = 255;

	// Endless Countermeasure Suite perk: set the moment a hit punches THROUGH the shields, i.e. on
	// real hull damage. Taken here rather than by comparing armor before/after, because the armor
	// deduction below is skipped under cheatInfiniteArmor, which would otherwise disarm
	// the perk while testing with invincibility on.
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
			// Make the block READ as its own event: the full nine-point ring (the shield-absorb
			// flare, so the hit visibly stops AT the shield) plus S_CLINK, a sound nothing else in
			// the damage path uses. Reusing the shield's ordinary flare and S_SHIELD_HIT makes a
			// block indistinguishable from being hit normally, i.e. the boon reads as inert.
			JE_setupExplosion(this_player->x - 17, this_player->y - 12, 0, 14, false, !twoPlayerMode);
			JE_setupExplosion(this_player->x - 5 , this_player->y - 12, 0, 15, false, !twoPlayerMode);
			JE_setupExplosion(this_player->x + 7 , this_player->y - 12, 0, 16, false, !twoPlayerMode);
			JE_setupExplosion(this_player->x + 19, this_player->y - 12, 0, 17, false, !twoPlayerMode);
			JE_setupExplosion(this_player->x - 17, this_player->y + 2 , 0, 18, false, !twoPlayerMode);
			JE_setupExplosion(this_player->x + 19, this_player->y + 2 , 0, 19, false, !twoPlayerMode);
			JE_setupExplosion(this_player->x - 17, this_player->y + 16, 0, 20, false, !twoPlayerMode);
			JE_setupExplosion(this_player->x - 5 , this_player->y + 16, 0, 21, false, !twoPlayerMode);
			JE_setupExplosion(this_player->x + 7 , this_player->y + 16, 0, 22, false, !twoPlayerMode);
			soundQueue[4] = S_CLINK;   // the "deflected" cue; deliberately NOT S_SHIELD_HIT / S_HULL_HIT
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

		JE_setupExplosion(this_player->x - 17, this_player->y - 12, 0, 14, false, !twoPlayerMode);
		JE_setupExplosion(this_player->x - 5 , this_player->y - 12, 0, 15, false, !twoPlayerMode);
		JE_setupExplosion(this_player->x + 7 , this_player->y - 12, 0, 16, false, !twoPlayerMode);
		JE_setupExplosion(this_player->x + 19, this_player->y - 12, 0, 17, false, !twoPlayerMode);

		JE_setupExplosion(this_player->x - 17, this_player->y + 2, 0,  18, false, !twoPlayerMode);
		JE_setupExplosion(this_player->x + 19, this_player->y + 2, 0,  19, false, !twoPlayerMode);

		JE_setupExplosion(this_player->x - 17, this_player->y + 16, 0, 20, false, !twoPlayerMode);
		JE_setupExplosion(this_player->x - 5 , this_player->y + 16, 0, 21, false, !twoPlayerMode);
		JE_setupExplosion(this_player->x + 7 , this_player->y + 16, 0, 22, false, !twoPlayerMode);
	}

	// Arm presentation-only gauge flashes only on the live pass; rollback must not restart them.
	const int gi = (this_player == &player[1]) ? 1 : 0;
	if (!rollback_resim && gaugeFlashShield && this_player->shield < oldShield)
		shieldGaugeFlash[gi] = GAUGE_FLASH_START;
	if (!rollback_resim && gaugeFlashArmor && this_player->armor < oldArmor)
		armorGaugeFlash[gi] = GAUGE_FLASH_START;

	/* Kinetic Converter perk (endless): the generator share reads the impact, so it needs a shield
	 * that soaked something; the recharge and the sidekicks ride any hit. cmHullHit rather than an
	 * armor delta, because cheatInfiniteArmor skips the armor deduction. */
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

	// Countermeasure Suite perk (endless): a hit that reaches the HULL triggers a point-defense burst
	// that vaporises enemy projectiles around the ship (radius grows at 2 stacks), on a shared cooldown.
	if (endlessFxShip(this_player) && cmHullHit)
	{
		const int cmRadius = endlessPerkCountermeasureRadius();  // 0 unless owned AND off cooldown
		if (cmRadius > 0)
		{
			// "Within N pixels" is measured from the SHIP, not from its centre reference point. The
			// hull already spans +-shot_hit_area (12x10), and anything inside that has by definition
			// just hit you (the enemy-shot loop frees that bullet BEFORE calling us), so a bare N
			// from the centre leaves only an empty ~14px halo. Reach N PAST the hitbox instead.
			const int reachX = (int)this_player->shot_hit_area_x + cmRadius;
			const int reachY = (int)this_player->shot_hit_area_y + cmRadius;

			for (int es = 0; es < ENEMY_SHOT_MAX; ++es)
			{
				if (!enemyShotAvail[es]
				    && abs(enemyShot[es].sx - this_player->x) <= reachX
				    && abs(enemyShot[es].sy - this_player->y) <= reachY)
				{
					JE_setupExplosion(enemyShot[es].sx, enemyShot[es].sy, 0, 0, false, false);
					enemyShotAvail[es] = true;
				}
			}

			// A ring flare at the sweep's edge so the burst always reads, even when it caught nothing.
			JE_setupExplosion(this_player->x - reachX, this_player->y, 0, 0, false, false);
			JE_setupExplosion(this_player->x + reachX, this_player->y, 0, 0, false, false);
			JE_setupExplosion(this_player->x, this_player->y - reachY, 0, 0, false, false);
			JE_setupExplosion(this_player->x, this_player->y + reachY, 0, 0, false, false);
			soundQueue[4] = S_WEAPON_7;    // point-defense "thunk"
			endlessCountermeasureFired();  // re-arm the cooldown
		}
	}

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

/* How many firing patterns one ship's rear bay offers. Per ship, because every caller clamps a
 * named ship's weapon_mode: online, both machines simulate both ships, and answering with the
 * local ship's count had a partner's toggle wrap against the wrong gun on one side, so the mode
 * change never reached the other client. */
JE_word JE_portConfigs(const Player *this_player)
{
	return tempW = weaponPort[this_player->items.weapon[REAR_WEAPON].id].opnum;
}

// Dim the remote ship's gauges after each repaint; the shade never compounds.
static bool gauge_is_remote(uint i)
{
	return isNetworkGame && thisPlayerNum >= 1 && thisPlayerNum <= 2 && i != thisPlayerNum - 1;
}

static void gauge_dim_rect(int x1, int y1, int x2, int y2)
{
	const int s = gauge_scale;
	SDL_Surface *const dst = gauge_surface();

	// Two shade passes (each halves the in-bank shade): a single one read too close to live.
	JE_barShade(dst, x1 * s, y1 * s, (x2 + 1) * s - 1, (y2 + 1) * s - 1);
	JE_barShade(dst, x1 * s, y1 * s, (x2 + 1) * s - 1, (y2 + 1) * s - 1);
}

// Label two-player gauge blocks in the shield bank, before the level fade-in.
void JE_drawPlayerTags(void)
{
	if (!split_arcade_mode() || galagaMode)
		return;

	for (uint i = 0; i < COUNTOF(player); ++i)
		JE_textShade(VGAScreen, HUD_X(289), 59 + 134 * i, (i == 0) ? "P1" : "P2", 0, 5, FULL_SHADE);
}

// Cap two-player gauge units to the 45-row region JE_wipeShieldArmorBars clears.
#define HUD_2P_GAUGE_UNITS_MAX 21

// Extend two-player bars into the final row covered by the wipe.
#define HUD_2P_GAUGE_TOP_PAD 1
static float hud_2p_gauge_units(float value)
{
	const float units = value * 0.8f;
	return (units > HUD_2P_GAUGE_UNITS_MAX) ? (float)HUD_2P_GAUGE_UNITS_MAX : units;
}

// The tick mark showing where a full shield would reach, drawn on the row JE_dBar3 would use as
// the bar's top at `units_max` (same edge arithmetic, so the two always meet). One 1x row thick
// whatever the scale. Only worth drawing while the bar is short of it.
static void draw_shield_ceiling_mark(int x, int bottom_y, float units_now, float units_max, int top_pad)
{
	if (units_now >= units_max)
		return;

	const int s = gauge_scale;
	const int y = (int)ceilf((float)((bottom_y - top_pad) * s) - (2.0f * units_max + 1.0f) * (float)s);
	if (y < 0)
		return;
	fill_rectangle_xy(gauge_surface(), x * s, y, (x + 9) * s - 1, y + s - 1, 68); /* <MXD> SEGa000 */
}

void JE_drawShield(void)
{
	if (rollback_resim_silent)
	{
		hud_bars_dirty = true;
		return;
	}

	if (split_arcade_mode() && !galagaMode)
	{
		for (uint i = 0; i < COUNTOF(player); ++i)
		{
			if (player_is_out(i))
				continue;

			const float units = hud_2p_gauge_units(gauge_shield_level(i));
			JE_dBar3_scaled(gauge_surface(), HUD_X(270), 60 + 134 * i, units, 144, gaugeGradShield, gauge_flash_render(shieldGaugeFlash[i]), HUD_2P_GAUGE_TOP_PAD, gauge_scale);
			// Before the dim, so a remote player's mark fades with the rest of their gauge.
			draw_shield_ceiling_mark(HUD_X(270), 60 + 134 * i, units, hud_2p_gauge_units((float)player[i].shield_max), HUD_2P_GAUGE_TOP_PAD);
			if (gauge_is_remote(i))
				gauge_dim_rect(HUD_X(270), 60 + 134 * i - 44, HUD_X(278), 60 + 134 * i);
		}
	}
	else
	{
		const uint i = gameplay_local_player_index();
		if (player_is_out(i))
			return;

		const float units = gauge_shield_level(i);
		JE_dBar3_scaled(gauge_surface(), HUD_X(270), 194, units, 144, gaugeGradShield, gauge_flash_render(shieldGaugeFlash[i]), 0, gauge_scale);
		draw_shield_ceiling_mark(HUD_X(270), 194, units, (float)player[i].shield_max, 0);
	}
}

// Endless reinforced hulls can exceed the 28-unit armour bar. Draw the overflow as stacked
// "rollover" layers: each full 28 units rolls the bar over and the next chunk fills from the
// bottom in a different colour gradient, so a heavily-reinforced hull reads as a stacked, multi-
// hued bar. Layer palette bases are palette-relative (endless levels vary); tuned by eye.
static void endlessDrawArmorBar(float armor, int flash)
{
	static const int layerCol[] = { 224, 112, 80, 176, 16, 48, 96, 32 };
	const int maxLayers = (int)COUNTOF(layerCol);

	int total = 0;
	for (float t = armor; t > 0.0f && total < maxLayers; t -= 28.0f)
		++total;
	const int flashLayer = (total <= 1) ? 0 : total - 1;

	float a = armor;
	for (int layer = 0; a > 0.0f && layer < maxLayers; ++layer)
	{
		const float seg = (a > 28.0f) ? 28.0f : a;
		JE_dBar3_scaled(gauge_surface(), HUD_X(307), 194, seg, layerCol[layer], gaugeGradArmor, (layer == flashLayer) ? flash : 0, 0, gauge_scale);
		a -= 28.0f;
	}
}

void JE_drawArmor(void)
{
	// The 28 cap is the classic bar maximum; the endless reinforced hull legitimately exceeds it
	// (drawn as rollover layers below), so don't clobber the real value in endless mode.
	// This clamp mutates sim state, so it must run on silent passes too, BEFORE the gate.
	if (!endlessFxActive())
		for (uint i = 0; i < COUNTOF(player); ++i)
			if (player[i].armor > 28)
				player[i].armor = 28;

	if (rollback_resim_silent)
	{
		hud_bars_dirty = true;
		return;
	}

	if (split_arcade_mode() && !galagaMode)
	{
		for (uint i = 0; i < COUNTOF(player); ++i)
		{
			if (player_is_out(i))
				continue;

			JE_dBar3_scaled(gauge_surface(), HUD_X(307), 60 + 134 * i, hud_2p_gauge_units(gauge_armor_level(i)), 224, gaugeGradArmor, gauge_flash_render(armorGaugeFlash[i]), HUD_2P_GAUGE_TOP_PAD, gauge_scale);
			if (gauge_is_remote(i))
				gauge_dim_rect(HUD_X(307), 60 + 134 * i - 44, HUD_X(315), 60 + 134 * i);
		}
	}
	else if (endlessFxActive())
	{
		const uint i = gameplay_local_player_index();
		if (player_is_out(i))
			return;

		endlessDrawArmorBar(gauge_armor_level(i), gauge_flash_render(armorGaugeFlash[i]));
	}
	else
	{
		const uint i = gameplay_local_player_index();
		if (player_is_out(i))
			return;

		JE_dBar3_scaled(gauge_surface(), HUD_X(307), 194, gauge_armor_level(i), 224, gaugeGradArmor, gauge_flash_render(armorGaugeFlash[i]), 0, gauge_scale);
	}
}

bool superpixelClipActive = false;
int superpixelClipX0, superpixelClipY0, superpixelClipX1, superpixelClipY1;

void JE_setSPClip(int x0, int y0, int x1, int y1)
{
	superpixelClipActive = true;
	superpixelClipX0 = x0;
	superpixelClipY0 = y0;
	superpixelClipX1 = x1;
	superpixelClipY1 = y1;
}

void JE_clearSPClip(void)
{
	superpixelClipActive = false;
}

// Sprites hiding this tick's occluded sparks. A shower thrown from the middle of a sprite would
// otherwise plot over it, since JE_drawSP runs after every playfield draw. Small: only the endless
// "?" pickup publishes a box, and only while it is on screen.
#define MAX_SP_OCCLUDERS 24  // sprites past this simply stop hiding sparks

static struct { int x0, y0, x1, y1; } sp_occluders[MAX_SP_OCCLUDERS];
static unsigned int sp_occluder_count;

void JE_addSPOccluder(int x0, int y0, int x1, int y1)
{
	if (sp_occluder_count >= COUNTOF(sp_occluders))
		return;

	sp_occluders[sp_occluder_count].x0 = x0;
	sp_occluders[sp_occluder_count].y0 = y0;
	sp_occluders[sp_occluder_count].x1 = x1;
	sp_occluders[sp_occluder_count].y1 = y1;
	++sp_occluder_count;
}

// Only an on-screen spark spawned with `occluded` reaches this, and the list is empty or one box
// long in nearly every frame.
static bool sp_hidden(int x, int y)
{
	for (unsigned int i = 0; i < sp_occluder_count; ++i)
	{
		if (x >= sp_occluders[i].x0 && x <= sp_occluders[i].x1 &&
		    y >= sp_occluders[i].y0 && y <= sp_occluders[i].y1)
			return true;
	}
	return false;
}

// Spawn slot for the next spark. The shared cursor advances exactly as it did when every source
// wrote to it, so a capped weapon trail is still thinned by the whole screen's spark traffic. Under
// Extra Sparks an uncapped spark retires the classic slot it would have taken and is written to the
// high window instead, which keeps that thinning without shortening the small uncapped showers.
// See doc/notes.md, "Superspark ring buffer".
static unsigned int next_superpixel(bool classic_cap)
{
	const unsigned int cap = (extraSparks && !classic_cap) ? MAX_SUPERPIXELS : SUPERPIXELS_CLASSIC;

	if (++last_superpixel >= cap)
		last_superpixel = 0;

	if (classic_cap || !extraSparks)
		return last_superpixel;

	if (last_superpixel < SUPERPIXELS_CLASSIC)
		superpixels[last_superpixel].z = 0;

	if (++last_uncapped_superpixel >= MAX_SUPERPIXELS || last_uncapped_superpixel < SUPERPIXELS_CLASSIC)
		last_uncapped_superpixel = SUPERPIXELS_CLASSIC;
	return last_uncapped_superpixel;
}

void JE_resetSP(void)
{
	last_superpixel = 0;
	last_uncapped_superpixel = SUPERPIXELS_CLASSIC;
	sp_occluder_count = 0;
	memset(superpixels, 0, sizeof(superpixels));
}

void JE_doSP(JE_word x, JE_word y, JE_word num, JE_byte explowidth, JE_byte color, bool classic_cap) /* superpixels */
{
	// Local int counter, not the global JE_byte `temp`: `num` is a JE_word and callers can request
	// well over 255 sparks (e.g. damage/2+3 on a big hit), which a byte counter can't reach -> it
	// would wrap at 255 and loop forever. (The spark pool grew huge for Extra Sparks; see MAX_SUPERPIXELS.)
	for (int sp = 0; sp < num; sp++)
	{
		JE_real tempr = mt_rand_lt1() * (2 * M_PI);
		signed int tempy = roundf(cosf(tempr) * mt_rand_1() * explowidth);
		signed int tempx = roundf(sinf(tempr) * mt_rand_1() * explowidth);

		const unsigned int slot = next_superpixel(classic_cap);
		superpixels[slot].x = tempx + x;
		superpixels[slot].y = tempy + y;
		superpixels[slot].delta_x = tempx;
		superpixels[slot].delta_y = tempy + 1;
		superpixels[slot].color = color;
		superpixels[slot].bright = 0;
		superpixels[slot].occluded = false;
		superpixels[slot].z = 15;
	}
}

// JE_doSP driven by `seed` instead of the simulation RNG. Superpixels are not rollback state, so a
// presentation-only effect can spawn them this way without touching the deterministic stream.
void JE_doSPSeeded(JE_word x, JE_word y, JE_word num, JE_byte explowidth, JE_byte color,
                   bool classic_cap, JE_byte bright, bool occluded, Uint32 seed)
{
	for (int sp = 0; sp < num; sp++)
	{
		seed = seed * 1103515245u + 12345u;  // Numerical Recipes LCG; the high bits are the usable ones
		const JE_real angle = (JE_real)(seed >> 16) / 65536.0 * (2 * M_PI);
		seed = seed * 1103515245u + 12345u;
		const JE_real reach = (JE_real)(seed >> 16) / 65536.0;

		const signed int tempy = roundf(cosf(angle) * reach * explowidth);
		const signed int tempx = roundf(sinf(angle) * reach * explowidth);

		const unsigned int slot = next_superpixel(classic_cap);
		superpixels[slot].x = tempx + x;
		superpixels[slot].y = tempy + y;
		superpixels[slot].delta_x = tempx;
		superpixels[slot].delta_y = tempy + 1;
		superpixels[slot].color = color;
		superpixels[slot].bright = bright;
		superpixels[slot].occluded = occluded;
		superpixels[slot].z = 15;
	}
}

void JE_drawSP(void)
{
	for (int i = MAX_SUPERPIXELS; i--; )
	{
		if (superpixels[i].z)
		{
			superpixels[i].x += superpixels[i].delta_x;
			superpixels[i].y += superpixels[i].delta_y;

			if (superpixels[i].x < (unsigned)VGAScreen->w && superpixels[i].y < (unsigned)VGAScreen->h
			    && !(superpixels[i].occluded && sp_hidden((int)superpixels[i].x, (int)superpixels[i].y))
			    && (!superpixelClipActive
			        || (superpixels[i].x >= (unsigned)superpixelClipX0 && superpixels[i].x < (unsigned)superpixelClipX1
			            && superpixels[i].y >= (unsigned)superpixelClipY0 && superpixels[i].y < (unsigned)superpixelClipY1)))
			{
				const Uint8 z = superpixels[i].z;
				const Uint8 color = superpixels[i].color;
				const Uint8 bright = superpixels[i].bright;

				// Record for the render list so the spark interpolates smoothly at the
				// display rate (constant velocity -> the per-tick delta is self-contained).
				if (render_list_recording)
					rl_rec_superpixel(superpixels[i].x, superpixels[i].y, superpixels[i].delta_x,
					                  superpixels[i].delta_y, z, color, bright);

				Uint8 *s = (Uint8 *)VGAScreen->pixels; /* screen pointer, 8-bit specific */
				s += superpixels[i].y * VGAScreen->pitch;
				s += superpixels[i].x;

				*s = rl_superpixel_value(*s, z, color, bright);
				if (superpixels[i].x > 0)
					*(s - 1) = rl_superpixel_value(*(s - 1), z >> 1, color, bright >> 1);
				if (superpixels[i].x < VGAScreen->w - 1u)
					*(s + 1) = rl_superpixel_value(*(s + 1), z >> 1, color, bright >> 1);
				if (superpixels[i].y > 0)
					*(s - VGAScreen->pitch) = rl_superpixel_value(*(s - VGAScreen->pitch), z >> 1, color, bright >> 1);
				if (superpixels[i].y < VGAScreen->h - 1u)
					*(s + VGAScreen->pitch) = rl_superpixel_value(*(s + VGAScreen->pitch), z >> 1, color, bright >> 1);
			}

			superpixels[i].z--;
		}
	}

	sp_occluder_count = 0;  // the boxes describe the frame just drawn; the next one republishes
}

/* Register this file's private simulation state. Public globals are registered
 * centrally in rollback_state.c. */
#include "rollback.h"

void varz_register_rollback(void)
{
	rollback_register("vz.flareFromTwiddle",    &flareFromTwiddle, sizeof(flareFromTwiddle));
	rollback_register("vz.twiddleFlareWait",    &twiddleFlareShotWait, sizeof(twiddleFlareShotWait));
	rollback_register("vz.twiddleWait",         &twiddleWait, sizeof(twiddleWait));
}
