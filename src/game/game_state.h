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
#ifndef GAME_STATE_H
#define GAME_STATE_H

#include "episodes.h"
#include "opentyr.h"
#include "player.h"
#include "sprite.h"

#include <stdbool.h>

#define SA 9

enum
{
	SA_NONE = 0,
	SA_NORTSHIPZ = 7,
	SA_LASTSHIP = 9,
	
	// only used for code entry
	SA_DESTRUCT = 10,
	SA_ENGAGE = 11,
	
	// only used in pItems[P_SUPERARCADE]
	SA_SUPERTYRIAN = 254,
	SA_ARCADE = 255
};

// Normal play keeps the original 60-shot cap; Endless uses the larger array. Keep
// RL_ID_ESHOT_BASE + ENEMY_SHOT_MAX below RL_ID_EXPL_BASE.
#define ENEMY_SHOT_NORMAL  60   // non-endless cap (= original behaviour)
#define ENEMY_SHOT_MAX     500  // array size + endless cap

#define CURRENT_KEY_SPEED 1  /*Keyboard/Joystick movement rate*/

/* Hundredths: the unit JE_SingleEnemyType.damageAccum and enemy_hp_divisor100 are counted in, so a
 * fractional HP multiplier can be spent exactly. */
#define ENEMY_DAMAGE_ACCUM_SCALE 100

struct JE_SingleEnemyType
{
	JE_byte     fillbyte;
	JE_integer  ex, ey;     /* POSITION */
	JE_shortint exc, eyc;   /* CURRENT SPEED */
	JE_shortint exca, eyca; /* RANDOM ACCELERATION */
	JE_shortint excc, eycc; /* FIXED ACCELERATION WAITTIME */
	JE_shortint exccw, eyccw;
	JE_byte     armorleft;
	/* Damage banked against a scaled hull, in ENEMY_DAMAGE_ACCUM_SCALE units per armor point.
	 * enemy_hp_divisor100 of them buy one point; see enemy_spend_damage. */
	int         damageAccum;
	JE_byte     healthbar_max;  /* armor this slot started with; its HP bar divides by this */
	JE_boolean  healthbar_seen; /* set once this enemy has taken player damage (gates the enemy HP bar) */
	JE_byte     eshotwait[3], eshotmultipos[3]; /* [1..3] */
	JE_byte     eshotextracredit[3]; /* endless carry for whole-volley tide scaling */
	JE_byte     enemycycle;
	JE_byte     ani;
	JE_word     egr[20]; /* [1..20] */
	JE_byte     size;
	JE_byte     linknum;
	JE_byte     aniactive;
	JE_byte     animax;
	JE_byte     aniwhenfire;
	Sprite2_array *sprite2s;
	JE_shortint exrev, eyrev;
	JE_integer  exccadd, eyccadd;
	JE_byte     exccwmax, eyccwmax;
	void       *enemydatofs;
	JE_boolean  edamaged;
	JE_word     enemytype;
	JE_byte     animin;
	JE_word     edgr;
	JE_shortint edlevel;
	JE_shortint edani;
	JE_byte     eliteState; /* endless: 0=undecided, 1=normal, 2=elite, 3=champion (endless_combat.c) */
	JE_boolean  groupHomed;
	JE_byte     homeTarget; /* endless co-op: which ship this enemy's tracking chases, 0 or 1 */
	JE_byte     filter;
	JE_integer  evalue;
	JE_integer  fixedmovey;
	// Scroll-scaling carry; positive selects a divisor, -100 a percentage, 0 resets.
	JE_integer  fixedmovey_carry;
	JE_integer  fixedmovey_carry_base;
	JE_integer  fixedmovey_carry_move;
	JE_byte     freq[3]; /* [1..3] */
	JE_byte     launchwait;
	JE_word     launchtype;
	JE_byte     launchfreq;
	JE_byte     xaccel;
	JE_byte     yaccel;
	JE_byte     tur[3]; /* [1..3] */
	JE_word     enemydie; /* Enemy created when this one dies */
	JE_boolean  enemyground;
	JE_byte     explonum;
	JE_integer  mapoffset;
	float       mapoffset_frac;  // sub-pixel part of the parallax anchor, for the smooth-H health bar
	JE_integer  scroll_ybase;    // whole-pixel draw-phase correction for the smooth-V health bar
	float       scroll_yfrac;    // fractional vertical layer phase for the smooth-V health bar
	JE_byte     scroll_ylayer;   // vertical background binding (0=none, 1..3=layer)
	JE_boolean  scoreitem;

	JE_boolean  special;
	JE_byte     flagnum;
	JE_boolean  setto;

	JE_byte     iced; /*Duration*/

	JE_byte     launchspecial;

	JE_integer  xminbounce;
	JE_integer  xmaxbounce;
	JE_integer  yminbounce;
	JE_integer  ymaxbounce;
	JE_byte     fill[3]; /* [1..3] */
	/* Last Chain Reaction wave to land here; one lands once (enemy_death.c). */
	JE_word     chainWave;
	JE_integer  approach_debt_y100;
};

typedef struct JE_SingleEnemyType JE_MultiEnemyType[100]; /* [1..100] */

typedef JE_byte JE_DanCShape[24 * 28]; /* [1..(24*28) div 2] OF WORD */

typedef JE_byte *JE_MapType[300][14]; /* [1..300, 1..14] */
typedef JE_byte *JE_MapType2[600][14]; /* [1..600, 1..14] */
typedef JE_byte *JE_MapType3[600][15]; /* [1..600, 1..15] */

struct JE_EventRecType
{
	JE_word     eventtime;
	JE_byte     eventtype;
	JE_integer  eventdat, eventdat2;
	JE_shortint eventdat3, eventdat5, eventdat6;
	JE_byte     eventdat4;
};

struct JE_MegaDataType1
{
	JE_MapType mainmap;
	struct
	{
		JE_DanCShape sh;
	} shapes[72]; /* [0..71] */
	JE_byte tempdat1;
	/*JE_DanCShape filler;*/
};

struct JE_MegaDataType2
{
	JE_MapType2 mainmap;
	struct
	{
		JE_byte nothing[3]; /* [1..3] */
		JE_byte fill;
		JE_DanCShape sh;
	} shapes[71]; /* [0..70] */
	JE_byte tempdat2;
};

struct JE_MegaDataType3
{
	JE_MapType3 mainmap;
	struct
	{
		JE_byte nothing[3]; /* [1..3] */
		JE_byte fill;
		JE_DanCShape sh;
	} shapes[70]; /* [0..69] */
	JE_byte tempdat3;
};

typedef JE_byte JE_EnemyAvailType[100]; /* [1..100] */

typedef struct {
	JE_integer sx, sy;
	JE_integer sxm, sym;
	JE_shortint sxc, syc;
	JE_byte tx, ty;
	JE_word sgr;
	JE_byte sdmg;
	JE_byte duration;
	JE_word animate;
	JE_word animax;
	// Course-correction state uses two reserved bytes, preserving this structure's size.
	JE_byte seekerArm;
	JE_byte seekerLeft;
	JE_byte filter;  // shooter's tier bank, stamped at spawn; 0 for the sprite's own colours
	JE_byte fill[9];
} EnemyShotType;

extern JE_integer tempDat, tempDat2, tempDat3;
extern const JE_word SASpecialWeapon[SA];
extern const JE_byte SAShip[SA];
extern const JE_word SAWeapon[SA][5];
extern JE_byte lvlFileNum;
// One-shot override for the next level load; zero uses the section default.
extern JE_byte forcedLvlFileNum;
extern JE_word maxEvent, eventLoc;
extern struct JE_EventRecType eventRec[EVENT_MAXIMUM];
extern JE_word levelEnemy[40], levelEnemyMax;
extern JE_word tempBackMove, explodeMove;
extern JE_byte levelEnd;
extern JE_word levelEndFxWait;
extern JE_shortint levelEndWarp;
extern JE_boolean endLevel, reallyEndLevel, waitToEndLevel, playerEndLevel, normalBonusLevelCurrent, bonusLevelCurrent, smallEnemyAdjust, readyToEndLevel, quitRequested;
extern JE_byte newPL[10];
extern JE_word returnLoc;
extern JE_boolean returnActive;
extern JE_word galagaShotFreq;
extern JE_longint galagaLife;
extern JE_boolean debug;
extern Uint32 debugTime, lastDebugTime;
extern JE_longint debugHistCount;
extern JE_real debugHist;
extern JE_word curLoc;
extern JE_boolean firstGameOver, gameLoaded, enemyStillExploding;
extern JE_word totalEnemy;
extern JE_word enemyKilled;
extern struct JE_MegaDataType1 megaData1;
extern struct JE_MegaDataType2 megaData2;
extern struct JE_MegaDataType3 megaData3;
extern JE_byte flash;
extern JE_shortint flashChange;
extern JE_byte displayTime;

extern JE_byte soundQueue[8];
extern JE_boolean enemyContinualDamage;
extern JE_boolean enemiesActive;
extern JE_boolean forceEvents;
extern JE_boolean stopBackgrounds;
extern JE_byte stopBackgroundNum;
extern JE_byte damageRate;
extern JE_boolean background3x1;
extern JE_boolean background3x1b;
extern JE_boolean levelTimer;
extern JE_word levelTimerCountdown;
extern JE_word levelTimerJumpTo;
extern JE_boolean randomExplosions;
extern JE_boolean globalFlags[10];
extern JE_byte levelSong;
extern JE_boolean loadDestruct;
extern JE_boolean moveTyrianLogoUp;
extern JE_boolean skipStarShowVGA;
extern JE_MultiEnemyType enemy;
extern JE_EnemyAvailType enemyAvail;
extern JE_word enemyOffset;
extern JE_word enemyOnScreen;
extern JE_word enemyParkedAbove;
extern JE_word mapStopStallTicks;
extern JE_word superEnemy254Jump;
extern JE_boolean enemyShotAvail[ENEMY_SHOT_MAX];
extern EnemyShotType enemyShot[ENEMY_SHOT_MAX];

extern JE_boolean allPlayersGone;
extern JE_word neat;
extern JE_byte temp, temp2, temp3;
extern JE_word tempW;
extern JE_boolean doNotSaveBackup;
extern JE_word x, y;
extern JE_integer b;
extern JE_byte **BKwrap1to, **BKwrap2to, **BKwrap3to, **BKwrap1, **BKwrap2, **BKwrap3;

#endif /* GAME_STATE_H */
