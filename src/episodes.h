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
#ifndef EPISODES_H
#define EPISODES_H

#include "opentyr.h"

#include "lvlmast.h"
#include "sprite.h"

/* Episodes and general data */

#define FIRST_LEVEL 1
#define EPISODE_MAX 5
#define EPISODE_AVAILABLE 5

// Width of a weapon's per-bullet arrays; raised from the original 8 so the Custom Weapon
// Creator can build wider weapons (stock data still loads exactly 8 per array; only custom
// weapons touch the extra slots; the shots.c wrap guard and editor clamps key off this).
// MUST stay in 8..255: multi/max and the fire cursor shotMultiPos are all bytes.
#define WEAPON_MULTI_MAX 255

typedef struct
{
	JE_word     drain;
	JE_byte     shotrepeat;
	JE_byte     multi;
	JE_word     weapani;
	JE_byte     max;
	JE_byte     tx, ty, aim;
	JE_byte     attack[WEAPON_MULTI_MAX], del[WEAPON_MULTI_MAX]; /* [1..WEAPON_MULTI_MAX]; on-disk data fills [1..8] */
	JE_shortint sx[WEAPON_MULTI_MAX], sy[WEAPON_MULTI_MAX];
	JE_shortint bx[WEAPON_MULTI_MAX], by[WEAPON_MULTI_MAX];
	JE_word     sg[WEAPON_MULTI_MAX];
	JE_shortint acceleration, accelerationx;
	JE_byte     circlesize;
	JE_byte     sound;
	JE_byte     trail;
	JE_byte     shipblastfilter;
} JE_WeaponType;

typedef struct
{
	char    name[31]; /* string [30] */
	JE_byte opnum;
	JE_word op[2][11]; /* [1..2, 1..11] */
	JE_word cost;
	JE_word itemgraphic;
	JE_word poweruse;
} JE_WeaponPortType[PORT_NUM + 1]; /* [0..portnum] */

typedef struct
{
	char        name[31]; /* string [30] */
	JE_word     itemgraphic;
	JE_byte     power;
	JE_shortint speed;
	JE_word     cost;
} JE_PowerType[POWER_NUM + 1]; /* [0..powernum] */

typedef struct
{
	char    name[31]; /* string [30] */
	JE_word itemgraphic;
	JE_byte pwr;
	JE_byte stype;
	JE_word wpn;
} JE_SpecialType[SPECIAL_NUM + 1]; /* [0..specialnum] */

typedef struct
{
	char        name[31]; /* string [30] */
	JE_byte     pwr;
	JE_word     itemgraphic;
	JE_word     cost;
	JE_byte     tr, option;
	JE_shortint opspd;
	JE_byte     ani;
	JE_word     gr[20]; /* [1..20] */
	JE_byte     wport;
	JE_word     wpnum;
	JE_byte     ammo;
	JE_boolean  stop;
	JE_byte     icongr;
} JE_OptionType;

typedef struct
{
	char    name[31]; /* string [30] */
	JE_byte tpwr;
	JE_byte mpwr;
	JE_word itemgraphic;
	JE_word cost;
} JE_ShieldType[SHIELD_NUM + 1]; /* [0..shieldnum] */

typedef struct
{
	char        name[31]; /* string [30] */
	JE_word     shipgraphic;
	JE_word     itemgraphic;
	JE_byte     ani;
	JE_shortint spd;
	JE_byte     dmg;
	JE_word     cost;
	JE_byte     bigshipgraphic;
} JE_ShipType[SHIP_DRAGONWING + 1]; /* [0..shipnum] plus the synthesized Dragonwing row */

/* EnemyData */
typedef struct
{
	JE_byte     ani;
	JE_byte     tur[3]; /* [1..3] */
	JE_byte     freq[3]; /* [1..3] */
	JE_shortint xmove;
	JE_shortint ymove;
	JE_shortint xaccel;
	JE_shortint yaccel;
	JE_shortint xcaccel;
	JE_shortint ycaccel;
	JE_integer  startx;
	JE_integer  starty;
	JE_shortint startxc;
	JE_shortint startyc;
	JE_byte     armor;
	JE_byte     esize;
	JE_word     egraphic[20];  /* [1..20] */
	JE_byte     explosiontype;
	JE_byte     animate;       /* 0:Not Yet   1:Always   2:When Firing Only */
	JE_byte     shapebank;     /* See LEVELMAK.DOC */
	JE_shortint xrev, yrev;
	JE_word     dgr;
	JE_shortint dlevel;
	JE_shortint dani;
	JE_byte     elaunchfreq;
	JE_word     elaunchtype;
	JE_integer  value;
	JE_word     eenemydie;
} JE_EnemyDatType[ENEMY_NUM + 1]; /* [0..enemynum] */

extern JE_WeaponPortType weaponPort;
extern JE_WeaponType weapons[WEAP_NUM + 1]; /* [0..weapnum] */
extern JE_PowerType powerSys;
extern JE_ShipType ships;
extern JE_OptionType options[OPTION_NUM + 1]; /* [0..optionnum] */
extern JE_ShieldType shields;
extern JE_SpecialType special;
extern JE_EnemyDatType enemyDat;
extern JE_byte initial_episode_num, episodeNum;
extern JE_boolean episodeAvail[EPISODE_MAX];

extern char episode_file[13], cube_file[13];

extern JE_longint episode1DataLoc;
extern JE_boolean bonusLevel;
extern JE_boolean jumpBackToEpisode1;
extern JE_byte chargeLaserSlot;  // option slot of the re-added Charge-Laser Cannon (0 = none)

// Two scratch weapon slots (in the unused WEAP_END1(818)..WEAP_START2(1000) gap, after
// the Charge-Laser's 900-905) holding the LV10-length Zica Lv11 side beams the "Long"
// length option fires. Built by JE_applyZicaLaserConfig; fired in mainint.c / game_menu.c.
#define ZICA_LONG_WEAP_LEFT  906
#define ZICA_LONG_WEAP_RIGHT 907

void JE_loadItemDat(void);
void JE_initEpisode(JE_byte newEpisode);

// Hand the shop sheet's 11 never-referenced 2x2 icons to the weapons/sidekicks that otherwise
// share another item's icon (or fall back to the 167 placeholder). Reads `unusedShopSprites`;
// restores the shipped icons when it is off, so the Visuals row takes effect between games
// without an item reload. Idempotent.
void JE_applyUnusedShopSprites(void);

/* Rewrite the loaded item data from every enhancement setting baked into it: the Zica Lv11
 * shape, the superspark trails, the ep1-3/ep4-5 item differences, and the shop icons. Idempotent,
 * so the menu calls it as a row changes and the change lands without starting a new game. */
void JE_applyItemDataSettings(void);

/* The firing sound an EpDiffWeapon row lands on in the given EPDIFF_* mode, so the Firing Sounds
 * menu can play what it just chose. 0 for a row whose two versions differ in something else. */
JE_byte JE_epDiffFiringSound(int item, int mode);

// Lower half of the shared "?" icon: a bare ship body, the base every rebuilt special icon sits on.
#define SPECIAL_ICON_SHIP_GR 125

// The unused player-shot sprite that replaces a shared special icon's upper half, or NULL when
// the special draws its own shipped 2x2. Reads `unusedShopSprites`; see draw_special_icon.
const Sprite2_array *JE_specialIconTop(JE_byte id, JE_word *gr);

// Display name of a special. Two records ship as "Pearl Wind", one firing a single aimed bolt and
// one spraying a flare-style field, and the endless grant pool can hand out either, so the bolt
// shows as "Pearl Shot" while endless effects are active. Every other name is the loaded one.
const char *JE_specialName(JE_byte id);

// Display name of a weapon port. The endless shop pools every real port into both gun menus,
// so the pairs that ship under one name (Protron, Multi-Cannon, Vulcan Cannon) are told apart
// while in endless. Every other name is the loaded one.
const char *JE_weaponPortName(JE_word id);

// Display name of a ship. The Nort Ship flies as the Nort Ship Z in endless. Every other name is
// the loaded one; an id past the table is named for the "None" record.
const char *JE_shipName(JE_word id);

// Refresh the "Ammo N" suffix on every ammo sidekick's shop name, so it shows the magazine the
// player will actually fly with (the endless Ordnance Reserves perk grows it mid-run). Guarded
// internally against no-op work, so the shop and JE_drawOptions can just call it.
void JE_labelAmmoSidekicks(void);

unsigned int JE_findNextEpisode(void);
void JE_scanForEpisodes(void);

#endif /* EPISODES_H */
