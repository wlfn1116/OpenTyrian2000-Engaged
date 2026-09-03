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
#include <string.h>

#include "episodes.h"

#include "config.h"
#include "custom_episode.h"
#include "custom_weapon.h"
#include "endless.h"
#include "file.h"
#include "lvllib.h"
#include "lvlmast.h"
#include "opentyr.h"
#include "sprite.h"

/* MAIN Weapons Data */
JE_WeaponPortType weaponPort;
JE_WeaponType     weapons[WEAP_NUM + 1]; /* [0..weapnum] */

/* Items */
JE_PowerType   powerSys;
JE_ShipType    ships;
JE_OptionType  options[OPTION_NUM + 1]; /* [0..optionnum] */
JE_ShieldType  shields;
JE_SpecialType special;

/* Enemy data */
JE_EnemyDatType enemyDat;

/* EPISODE variables */
JE_byte    initial_episode_num, episodeNum = 0;
JE_boolean episodeAvail[EPISODE_MAX]; /* [1..episodemax] */
char       episode_file[13], cube_file[13];

JE_longint episode1DataLoc;

/* Tells the game whether the level currently loaded is a bonus level. */
JE_boolean bonusLevel;

/* Tells if the game jumped back to Episode 1 */
JE_boolean jumpBackToEpisode1;

/* Reload when a custom and stock episode share the same base number. */
static JE_boolean episodeForceReload = false;

void JE_forceEpisodeReload(void)
{
	episodeForceReload = true;
}

const char *JE_episodeDir(void)
{
	return customEpisodeActive() ? custom_episode_dir() : data_dir();
}

// Re-adds the cut-from-Tyrian-2000 "Charge-Laser Cannon", a 5-stage DOS charge sidekick
// (sprites survive in spriteSheet9); values below are verbatim from the DOS LVLs.
#define CHARGELASER_WEAP_BASE 900  // 6 scratch weapon slots in the unused WEAP_END1(818)..WEAP_START2(1000) gap

// Option slot claimed for the current episode (differs per episode), or 0 if none
// was free; the shop loader (tyrian2.c) reads this.
JE_byte chargeLaserSlot = 0;

// Reuse newsh1.shp's unreferenced icons for weapons without distinct shop art.
static const struct { JE_byte port; JE_word gr; } unusedSpritePorts[] =
{
	{ 31,  15 },  // Guided Bombs
	{ 32, 191 },  // Shuruiken Field       (was the 167 placeholder)
	{ 33,  39 },  // Poison Bomb
	{ 34,  77 },  // Protron Wave          (was the 167 placeholder; shares Protron Z's icon)
	{ 35, 205 },  // The Orange Juicer     (was the 167 placeholder)
	{ 36,  17 },  // NortShip Super Pulse
	{ 37,  15 },  // NortShip Spreader
	{ 38,  43 },  // NortShip Spreader B
	{ 39, 191 },  // Atomic RailGun
	{ 41,  41 },  // Sonic Impulse         (shares Sonic Wave's icon)
	{ 42, 205 },  // RetroBall
	{ 44, 167 },  // Pretzel Missile
	{ 45,  43 },  // Dragon Frost
	{ 46, 167 },  // People Pretzels
	{ 47,  39 },  // Dragon Flame
};

// Sidekicks get the same treatment. The Charge-Laser's slot differs per episode, so it is
// resolved at capture time rather than hard-coded (0 = the toggle re-added nothing).
#define UNUSED_SPRITE_WOBBLEY          5  // "Wobbley" (verified in all three item tables)
#define UNUSED_SPRITE_ZICA_CHARGER    12  // "Zica SuperCharger"         (likewise)
#define UNUSED_SPRITE_TROPICAL_CHERRY 29  // "Tropical Cherry Companion" (likewise)
#define UNUSED_SPRITE_SATELLITE_MARLO 30  // "Satellite Marlo"           (likewise)
#define UNUSED_SPRITE_FLYING_PUNCH    32  // "Flying Punch"              (likewise)
static const struct { JE_byte opt; JE_word gr; } unusedSpriteOptions[] =
{
	{ UNUSED_SPRITE_WOBBLEY,         129 },
	{ UNUSED_SPRITE_ZICA_CHARGER,     45 },
	{ UNUSED_SPRITE_TROPICAL_CHERRY,   3 },  // shares the Banana Blast icon
	{ UNUSED_SPRITE_SATELLITE_MARLO, 205 },
	{ UNUSED_SPRITE_FLYING_PUNCH,    203 },
};
#define UNUSED_SPRITE_CHARGE_LASER_GR 17  // ...plus the Charge-Laser Cannon, slot resolved below

// Special itemgraphic values index spriteSheet10. Each replacement must be an otherwise unused
// 2x2; see doc/notes.md#special-pickups.
static const struct { JE_byte id; JE_word gr; } unusedSpecialIcons[] =
{
	{ 48, 53 },  // Dragon Lightning (was 93, Lightning Zone's)
};

// Eleven more have no spare icon to take: three icons between them, eight wearing the same "?".
static const struct { JE_byte id; JE_byte bank; JE_word gr; } unusedSpecialTops[] =
{
	{  2, 11,  25 },  // Pearl Wind, the instant-shot record; the field one keeps the shipped icon
	{  7,  7, 109 },  // Blade Field
	{  8,  7, 304 },  // SandStorm
	{ 11,  7, 154 },  // Banana Bomb
	{ 12,  7, 129 },  // Protron Dispersal
	{ 13,  7, 253 },  // Astral Zone
	{ 16,  7,  88 },  // Orange Shield
	{ 19,  7,  32 },  // Missile Pod
	{ 41,  7, 265 },  // SDF Main Gun
	{ 45,  7, 272 },  // 8-Way Microbomb
	{ 47, 11, 163 },  // Super Pretzel, which is People Pretzels' own bolt
};

// Snapshot shipped icons after item load so the toggle can restore them without a reload.
static JE_word unusedSpriteBasePort[COUNTOF(unusedSpritePorts)];
static JE_word unusedSpriteBaseOpt[COUNTOF(unusedSpriteOptions)];
static JE_word unusedSpriteBaseSpecial[COUNTOF(unusedSpecialIcons)];
static JE_word unusedSpriteBaseLaser;
static JE_byte unusedSpriteLaserSlot;
static bool    unusedSpriteCaptured = false;

// Snapshot the as-shipped icons. Runs after the 167 placeholder pass and after
// JE_applyChargeLaserCannon, so the baseline is what the shops would otherwise draw.
static void JE_captureUnusedShopSprites(void)
{
	for (unsigned int i = 0; i < COUNTOF(unusedSpritePorts); ++i)
		unusedSpriteBasePort[i] = weaponPort[unusedSpritePorts[i].port].itemgraphic;
	for (unsigned int i = 0; i < COUNTOF(unusedSpriteOptions); ++i)
		unusedSpriteBaseOpt[i] = options[unusedSpriteOptions[i].opt].itemgraphic;
	for (unsigned int i = 0; i < COUNTOF(unusedSpecialIcons); ++i)
		unusedSpriteBaseSpecial[i] = special[unusedSpecialIcons[i].id].itemgraphic;

	unusedSpriteLaserSlot = chargeLaserSlot;
	unusedSpriteBaseLaser = (chargeLaserSlot > 0) ? options[chargeLaserSlot].itemgraphic : 0;

	unusedSpriteCaptured = true;
}

void JE_applyUnusedShopSprites(void)
{
	if (!unusedSpriteCaptured)
		return;  // nothing loaded yet; the load path captures then calls us

	for (unsigned int i = 0; i < COUNTOF(unusedSpritePorts); ++i)
		weaponPort[unusedSpritePorts[i].port].itemgraphic =
			unusedShopSprites ? unusedSpritePorts[i].gr : unusedSpriteBasePort[i];

	for (unsigned int i = 0; i < COUNTOF(unusedSpriteOptions); ++i)
		options[unusedSpriteOptions[i].opt].itemgraphic =
			unusedShopSprites ? unusedSpriteOptions[i].gr : unusedSpriteBaseOpt[i];

	for (unsigned int i = 0; i < COUNTOF(unusedSpecialIcons); ++i)
		special[unusedSpecialIcons[i].id].itemgraphic =
			unusedShopSprites ? unusedSpecialIcons[i].gr : unusedSpriteBaseSpecial[i];

	// The Charge-Laser only exists while its own toggle is on; if it was never added, or the
	// slot moved since capture, leave it alone rather than writing into someone else's item.
	if (unusedSpriteLaserSlot > 0 && unusedSpriteLaserSlot == chargeLaserSlot)
		options[unusedSpriteLaserSlot].itemgraphic =
			unusedShopSprites ? UNUSED_SPRITE_CHARGE_LASER_GR : unusedSpriteBaseLaser;
}

const Sprite2_array *JE_specialIconTop(JE_byte id, JE_word *gr)
{
	if (!unusedShopSprites)
		return NULL;

	for (unsigned int i = 0; i < COUNTOF(unusedSpecialTops); ++i)
		if (unusedSpecialTops[i].id == id)
		{
			*gr = unusedSpecialTops[i].gr;
			return (unusedSpecialTops[i].bank == 11) ? &spriteSheet12 : &spriteSheet8;
		}

	return NULL;
}

const char *JE_specialName(JE_byte id)
{
	if (id > SPECIAL_NUM)
		return special[0].name;  // the "None" record

	// Matched by record data so it holds for both the ep1-3 and the ep4/5 item table.
	if (endlessFxActive() && special[id].stype == 1 &&
	    strncmp(special[id].name, "Pearl Wind", 10) == 0)
		return "Pearl Shot";

	return special[id].name;
}

// Port ids hold across the ep1-3 and ep4/5 item tables, and custom designs only claim the
// Test range (48..60), so an id match is safe for every loaded table.
const char *JE_weaponPortName(JE_word id)
{
	if (endlessMode)
	{
		switch (id)
		{
		case 2:  return "Front Multi-Cannon";
		case 10: return "Rear Multi-Cannon";
		case 12: return "Rear Protron";
		case 13: return "Front Protron";
		case 15: return "Twin Vulcan Cannon";
		}
	}
	return weaponPort[id].name;
}

// Ship ids hold across both item tables, so an id match is safe for every loaded table.
const char *JE_shipName(JE_word id)
{
	if (id > SHIP_DRAGONWING)
		return ships[0].name;  // the "None" record; a shipedit ship is named by its caller

	// Three hulls the Endless shop renames after their Super Arcade counterparts.
	if (endlessMode)
	{
		switch (id)
		{
		case 11: return "TX SilverCloud";
		case 12: return "Nort Ship Z";
		case 17: return "Pretzel Pete Truck";
		}
	}

	return ships[id].name;
}

/* Save the sidekick row replaced by Charge-Laser so the toggle can restore it. */
static int chargeLaserNativeSlot = 0;
static JE_OptionType chargeLaserNativeOption;
static bool chargeLaserCaptured = false;

static void JE_captureChargeLaserSlot(void)
{
	// The first free ("None") sidekick slot; free slots differ per episode, so this never
	// clobbers a real weapon. Names are space-padded to 30 chars, hence the prefix match.
	chargeLaserNativeSlot = 0;
	for (int i = 1; i <= OPTION_NUM; ++i)
	{
		if (strncmp(options[i].name, "None", 4) == 0)
		{
			chargeLaserNativeSlot = i;
			chargeLaserNativeOption = options[i];
			break;
		}
	}

	chargeLaserCaptured = true;
	chargeLaserSlot = 0;
}

static void JE_writeChargeLaserCannon(int slot)
{
	// Six DOS charge stages, weapons 452..457; unused fields remain zero.
	static const struct { JE_byte shotrepeat, attack; JE_word sg; } stage[6] =
	{
		{  4,  2, 260 },  // charge 0: fast, weak
		{  6,  3, 261 },  // charge 1
		{  8,  5, 262 },  // charge 2
		{ 10, 10, 263 },  // charge 3
		{ 12, 20, 264 },  // charge 4
		{ 14, 40, 265 },  // charge 5: slow, powerful
	};
	for (int k = 0; k < 6; ++k)
	{
		JE_WeaponType *w = &weapons[CHARGELASER_WEAP_BASE + k];
		memset(w, 0, sizeof(*w));
		w->shotrepeat      = stage[k].shotrepeat;
		w->multi           = 1;
		w->max             = 1;
		w->attack[0]       = stage[k].attack;
		w->del[0]          = 255;
		w->sy[0]           = 11;             // bolt travels straight up at speed 11
		w->sg[0]           = stage[k].sg;    // Charge-Laser bolt sprite (player-shot / spriteSheet8 260..265)
		w->sound           = 6;              // original's fire sound
		w->trail           = 255;
		w->shipblastfilter = 208;
	}

	// Its in-game sidekick frames (87/106/125/144, each x3), still in spriteSheet9.
	static const JE_word grFrames[20] =
		{ 87,87,87, 106,106,106, 125,125,125, 144,144,144 };

	JE_OptionType *o = &options[slot];
	memset(o, 0, sizeof(*o));
	strcpy(o->name, "Charge-Laser Cannon");
	o->pwr         = 5;      // five charge stages; its defining trait
	o->itemgraphic = 193;    // shop icon (matches the original record)
	o->cost        = 30000;
	o->tr          = 0;      // side-mounted (style 0 -> drawn from spriteSheet9)
	o->option      = 1;      // always animating
	o->opspd       = 3;
	o->ani         = 12;     // 4 frames x 3, as in the original
	memcpy(o->gr, grFrames, sizeof(o->gr));
	o->wport       = 4;      // power-drain port (as original; also the Zica Flamethrower's)
	o->wpnum       = CHARGELASER_WEAP_BASE;
	o->ammo        = 0;      // infinite; a charge weapon, not an ammo weapon
	o->stop        = true;
	o->icongr      = 6;
}

/* Put the Charge-Laser in its captured slot or take it back out, so the menu toggle applies
 * without an item-data reload. */
static void JE_applyChargeLaserCannon(void)
{
	if (!chargeLaserCaptured || chargeLaserNativeSlot == 0)
	{
		chargeLaserSlot = 0;  // no spare slot in this episode's item data
		return;
	}

	const bool mine = chargeLaserSlot == chargeLaserNativeSlot;

	if (chargeLaserCannon)
	{
		if (!mine && strncmp(options[chargeLaserNativeSlot].name, "None", 4) != 0)
			return;  // something else holds the slot; wait for the next item-data load

		JE_writeChargeLaserCannon(chargeLaserNativeSlot);
		chargeLaserSlot = chargeLaserNativeSlot;  // shops check for > 0
	}
	else if (mine)
	{
		options[chargeLaserNativeSlot] = chargeLaserNativeOption;
		chargeLaserSlot = 0;
	}
}

// The Zica Laser (port 5) Lv11 native horizontal layout, captured before we reshape it so
// ZICA_BASE_AUTO can restore the episode's vanilla pattern.
static JE_shortint zicaNativeSx[8], zicaNativeBx[8];
static bool zicaNativeCaptured = false;

// Prepare the configured Zica Lv11 short pattern and long side-beam templates. Fire-time code
// selects the templates and optional center beam.
static void JE_applyZicaLaserConfig(void)
{
	const int wn11 = weaponPort[5].op[0][10];  // Zica Laser (port 5), Lv11 weapon (209)
	const int wn10 = weaponPort[5].op[0][9];   // Lv10 weapon (208); the long-beam template
	if (wn11 <= 0 || wn11 > WEAP_NUM || wn10 <= 0 || wn10 > WEAP_NUM)
		return;

	// Effective horizontal pattern: spread (ep4) vs two straight columns (ep1-3).
	bool spread;
	if (zicaLaserBase == ZICA_BASE_EP13)
		spread = false;
	else if (zicaLaserBase == ZICA_BASE_EP4)
		spread = true;
	else  // ZICA_BASE_AUTO: whatever this episode's data shipped with
		spread = zicaNativeCaptured ? (zicaNativeSx[0] != 0) : true;

	static const JE_shortint sx_spread[8]   = { -1, -1, -1, -1,  1,  1,  1,  1 };
	static const JE_shortint bx_spread[8]   = {  0,  0,  0,  0,  0,  0,  0,  0 };
	static const JE_shortint sx_straight[8] = {  0,  0,  0,  0,  0,  0,  0,  0 };
	static const JE_shortint bx_straight[8] = { -8, -8, -8, -8,  8,  8,  8,  8 };

	// Shape the short level 11 weapon. Auto restores vanilla; forced modes replace
	// horizontal layout. Copy only the eight source slots.
	if (zicaLaserBase == ZICA_BASE_AUTO && zicaNativeCaptured)
	{
		memcpy(weapons[wn11].sx, zicaNativeSx, sizeof(zicaNativeSx));
		memcpy(weapons[wn11].bx, zicaNativeBx, sizeof(zicaNativeBx));
	}
	else if (zicaLaserBase != ZICA_BASE_AUTO)
	{
		memcpy(weapons[wn11].sx, spread ? sx_spread : sx_straight, sizeof(sx_spread));
		memcpy(weapons[wn11].bx, spread ? bx_spread : bx_straight, sizeof(bx_spread));
	}

	// Build long side beams from the Lv10 template. Lock keeps them ship-bound; otherwise columns
	// travel straight and the spread pattern drifts outward.
	memcpy(&weapons[ZICA_LONG_WEAP_LEFT],  &weapons[wn10], sizeof(JE_WeaponType));
	memcpy(&weapons[ZICA_LONG_WEAP_RIGHT], &weapons[wn10], sizeof(JE_WeaponType));
	for (int i = 0; i < 8; ++i)
	{
		JE_shortint lsx, rsx, lbx, rbx;
		if (zicaLaserLock)
		{
			lsx = rsx = 120;         // ship-locked (sentinel), glued to the ship
			lbx = -8; rbx = 8;       // 8px left/right so both columns are visible
		}
		else if (spread)
		{
			lsx = -1; rsx = 1;       // drift out from the centre (ep4)
			lbx = 0;  rbx = 0;
		}
		else
		{
			lsx = rsx = 0;           // straight up (ep1-3)
			lbx = -8; rbx = 8;
		}
		weapons[ZICA_LONG_WEAP_LEFT].sx[i]  = lsx;
		weapons[ZICA_LONG_WEAP_RIGHT].sx[i] = rsx;
		weapons[ZICA_LONG_WEAP_LEFT].bx[i]  = lbx;
		weapons[ZICA_LONG_WEAP_RIGHT].bx[i] = rbx;
	}
}

// Superspark trails: retags the ep4/5 ">1000" spark-shower shot graphics per superSparkMode
// Auto uses the shipped episode setting; On and Off force it. Idempotent.

// Retag one weapon's fire pattern between plain[i] and tagged[i] per the SUPER_SPARKS_* mode.
static void JE_retagWeaponSparks(int wn, int mode, const JE_word *plain, const JE_word *tagged, int nsprites)
{
	if (wn <= 0 || wn > WEAP_NUM)
		return;
	// Retag only the real repeating shot slots, not unused array padding.
	for (int j = 0; j < weapons[wn].max && j < 8; ++j)
	{
		for (int k = 0; k < nsprites; ++k)
		{
			if (weapons[wn].sg[j] == plain[k] || weapons[wn].sg[j] == tagged[k])
			{
				bool tag;
				switch (mode)
				{
				case SUPER_SPARKS_ON:  tag = true;  break;
				case SUPER_SPARKS_OFF: tag = false; break;
				default:               tag = (episodeNum > 3); break;  // Auto = the shipped value
				}
				weapons[wn].sg[j] = tag ? tagged[k] : plain[k];
				break;
			}
		}
	}
}

static void JE_applySuperSparks(void)
{
	// Mega Pulse: resolve the power-level weapons through the port table.
	static const JE_word pulsePlain[]  = { 35 },  pulseTagged[]  = { 7035 };
	for (int lvl = 0; lvl < 11; ++lvl)
		JE_retagWeaponSparks(weaponPort[19].op[0][lvl], superSparkMode[SSW_MEGA_PULSE],
		                     pulsePlain, pulseTagged, COUNTOF(pulsePlain));

	// Optionally rebuild Beno Wallop Beam's second bolt from the shipped Ep4/5 data.
	{
		JE_WeaponType *w = &weapons[736];
		bool second;
		switch (wallopSecondBolt)
		{
		case SUPER_SPARKS_ON:  second = true;  break;
		case SUPER_SPARKS_OFF: second = false; break;
		default:               second = (episodeNum > 3); break;  // Auto = the shipped pattern
		}
		if (second)
		{
			w->multi = 2;
			w->max   = 2;
			w->attack[1] = 10;
			w->del[1]    = 255;
			w->sx[1] = 0;  w->sy[1] = 10;
			w->bx[1] = 0;  w->by[1] = -2;
			w->sg[1] = 7029;  // spark-tagged like the shipped data; the retag applies the mode
		}
		else
		{
			w->multi = 1;  // slot 1 becomes unused padding again
			w->max   = 1;
		}
	}
	static const JE_word wallopPlain[] = { 30, 29 }, wallopTagged[] = { 7030, 7029 };
	JE_retagWeaponSparks(736, superSparkMode[SSW_WALLOP_BEAM], wallopPlain, wallopTagged, COUNTOF(wallopPlain));

	// Beno Protron System -B- sidekick (option wpnum 737).
	static const JE_word protronPlain[] = { 28 }, protronTagged[] = { 9028 };
	JE_retagWeaponSparks(737, superSparkMode[SSW_PROTRON_B], protronPlain, protronTagged, COUNTOF(protronPlain));

	// Ice Beam (special wpn 621) and Ice Blast (special wpn 706) share one setting: both
	// fire the same spark-tagged sprite.
	static const JE_word icePlain[] = { 634 }, iceTagged[] = { 9634 };
	JE_retagWeaponSparks(621, superSparkMode[SSW_ICE], icePlain, iceTagged, COUNTOF(icePlain));
	JE_retagWeaponSparks(706, superSparkMode[SSW_ICE], icePlain, iceTagged, COUNTOF(icePlain));
}

// Apply episode-specific item data from shipped constants. Auto keeps the running episode;
// only active pattern slots are rewritten.
/* This shared table keeps item-data changes and menu sound previews in sync. */
static const struct
{
	JE_byte port;
	JE_word weapon;
	JE_byte ep13, ep45;
} epDiffSounds[EDW_COUNT] = {
	[EDW_NEEDLE_LASER]    = { .port = 43,    .ep13 = 31, .ep45 = 13 },
	[EDW_BUBBLE_GUM]      = { .weapon = 792, .ep13 = 30, .ep45 = 13 },
	[EDW_FLYING_PUNCH]    = { .weapon = 794, .ep13 = 31, .ep45 = 30 },
	[EDW_PRETZEL_MISSILE] = { .port = 44,    .ep13 = 31, .ep45 = 30 },
	[EDW_DRAGON_FROST]    = { .port = 45,    .ep13 = 31, .ep45 = 30 },
};

// Give every weapon record a port can fire the same sound, across both firing modes.
static void JE_setPortFiringSound(JE_byte port, JE_byte sound)
{
	if (port == 0 || port > PORT_NUM)
		return;

	for (unsigned int mode = 0; mode < COUNTOF(weaponPort[port].op); ++mode)
		for (unsigned int level = 0; level < COUNTOF(weaponPort[port].op[0]); ++level)
		{
			const JE_word wpn = weaponPort[port].op[mode][level];
			if (wpn > 0 && wpn <= WEAP_NUM)
				weapons[wpn].sound = sound;
		}
}

JE_byte JE_epDiffFiringSound(int item, int mode)
{
	if (item < 0 || item >= EDW_COUNT
	    || (epDiffSounds[item].port == 0 && epDiffSounds[item].weapon == 0))
		return 0;

	const bool ep45 = mode == EPDIFF_EP45 || (mode == EPDIFF_AUTO && episodeNum > 3);

	return ep45 ? epDiffSounds[item].ep45 : epDiffSounds[item].ep13;
}

static void JE_applyEpDiffs(void)
{
	for (int w = 0; w < EDW_COUNT; ++w)
	{
		bool ep45;
		switch (epDiffMode[w])
		{
		case EPDIFF_EP13: ep45 = false;             break;
		case EPDIFF_EP45: ep45 = true;              break;
		default:          ep45 = (episodeNum > 3);  break;  // Auto = the shipped era
		}

		switch (w)
		{
		case EDW_XEGA_BALL:
		{
			// ep1-3: two balls, a six-step spreading pattern, 4 damage each.
			// ep4/5: a single ball, one step, 8 damage.
			JE_WeaponType *x = &weapons[720];
			if (ep45)
			{
				x->multi = 1;  x->max = 1;
				x->attack[0] = 8;  x->del[0] = 255;
				x->sx[0] = 0;  x->sy[0] = 10;  x->bx[0] = -20;  x->by[0] = -15;
				x->sg[0] = 60022;
			}
			else
			{
				static const JE_shortint sx[6] = { 0, 0, -8, 8, -10, 10 };
				static const JE_shortint sy[6] = { 10, 10, 8, 8, 0, 0 };
				static const JE_shortint bx[6] = { -20, -20, -30, -10, -40, 0 };
				static const JE_shortint by[6] = { -15, -15, -10, -10, -10, -10 };
				x->multi = 2;  x->max = 6;
				for (int i = 0; i < 6; ++i)
				{
					x->attack[i] = 4;  x->del[i] = 255;
					x->sx[i] = sx[i];  x->sy[i] = sy[i];  x->bx[i] = bx[i];  x->by[i] = by[i];
					x->sg[i] = 60022;
				}
			}
			break;
		}
		case EDW_MICROSOL_OPT5:
		{
			// MicroSol weapon 5 is an eight-way fan in episodes 1-3 and a cheap twin shot later.
			JE_WeaponType *m = &weapons[23];
			if (ep45)
			{
				m->drain = 40;  m->multi = 2;  m->max = 2;  m->acceleration = 0;
				m->attack[0] = 1;   m->attack[1] = 1;
				m->del[0] = 255;    m->del[1] = 255;
				m->sx[0] = -14;     m->sx[1] = -14;
				m->sy[0] = 0;       m->sy[1] = 0;
				m->bx[0] = -8;      m->bx[1] = 8;
				m->by[0] = 0;       m->by[1] = 0;
				m->sg[0] = 99;      m->sg[1] = 99;
			}
			else
			{
				static const JE_shortint sx[8] = { 1, -1, 2, -2, 3, -3, 4, -4 };
				static const JE_shortint sy[8] = { 3, 3, 2, 2, 1, 1, 0, 0 };
				m->drain = 160;  m->multi = 8;  m->max = 8;  m->acceleration = 1;
				for (int i = 0; i < 8; ++i)
				{
					m->attack[i] = 3;  m->del[i] = 255;
					m->sx[i] = sx[i];  m->sy[i] = sy[i];  m->bx[i] = 0;  m->by[i] = 0;
					m->sg[i] = 73;
				}
			}
			break;
		}
		case EDW_FLARE:
			// Flare / Super Bomb (wpn 622): the first four blast frames are sprite 20 (ep1-3)
			// or 21 (ep4/5); the rest of the pattern is identical.
			for (int i = 0; i < 4; ++i)
				weapons[622].sg[i] = ep45 ? 21 : 20;
			break;
		case EDW_NEEDLE_LASER:
		case EDW_BUBBLE_GUM:
		case EDW_FLYING_PUNCH:
		case EDW_PRETZEL_MISSILE:
		case EDW_DRAGON_FROST:
		{
			const JE_byte sound = ep45 ? epDiffSounds[w].ep45 : epDiffSounds[w].ep13;

			if (epDiffSounds[w].port != 0)
				JE_setPortFiringSound(epDiffSounds[w].port, sound);
			else
				weapons[epDiffSounds[w].weapon].sound = sound;
			break;
		}
		case EDW_SOLAR_SHIELD:
			// The one shop icon the two sets disagree on: ep1-3 gives the Gencore Solar Shield
			// the MicroCorp HXS Class C picture, ep4/5 the one its two Gencore siblings use.
			shields[8].itemgraphic = ep45 ? 153 : 165;
			break;
		case EDW_USHIP_PIC:
			// The two ships the sets illustrate differently: ep1-3 lends the U-Ship the Gencore
			// hull with detached wings, ep4/5 the broad USP delta the Talon and Fang use.
			ships[10].bigshipgraphic = ep45 ? 32 : 28;
			break;
		case EDW_NORTSHIP_PIC:
			// The Nort Ship gets the swept-wing Stalker hull in ep1-3, that same delta in ep4/5.
			ships[12].bigshipgraphic = ep45 ? 32 : 33;
			break;
		}
	}
}

// Rebuild aligned "Ammo N" shop labels from captured base names and magazine sizes.
// This also reflects Endless magazine bonuses and remains idempotent.
static char    ammoBaseName[OPTION_NUM + 1][31];
static JE_byte ammoBaseAmmo[OPTION_NUM + 1];
static int     ammoLabelPct = -1;  // magazine-bonus % the current labels were written for; -1 = not built yet

// Copy `src` (space-padded to 30 in the data) into `dst` with the padding and any trailing
// "Ammo <digits>" label removed, leaving just the weapon's own name.
static void JE_stripAmmoLabel(const char *src, char *dst, size_t dstsz)
{
	size_t len = strlen(src);
	while (len > 0 && src[len - 1] == ' ')
		--len;

	const char *p = strstr(src, "Ammo ");
	if (p != NULL)
	{
		const char *q = p + 5;
		while (*q >= '0' && *q <= '9')
			++q;
		if (q > p + 5 && (size_t)(q - src) >= len)  // the digits run to the end: it really is the label
		{
			len = (size_t)(p - src);
			while (len > 0 && src[len - 1] == ' ')
				--len;
		}
	}

	if (len > dstsz - 1)
		len = dstsz - 1;
	memcpy(dst, src, len);
	dst[len] = '\0';
}

// Snapshot the freshly-loaded sidekick names and magazine sizes, and force the next relabel to
// run. Called from JE_loadItemDat, which is the only thing that rewrites options[] from disk.
static void JE_captureAmmoSidekickBases(void)
{
	for (int i = 1; i <= OPTION_NUM; ++i)
	{
		ammoBaseAmmo[i] = options[i].ammo;
		JE_stripAmmoLabel(options[i].name, ammoBaseName[i], sizeof(ammoBaseName[i]));
	}
	ammoLabelPct = -1;
}

// Refresh ammo sidekick names from their effective Ordnance Reserves magazine size.
void JE_labelAmmoSidekicks(void)
{
	// Personal perks: the shop names show the magazine this machine's own player would fly.
	const uint fxSaved = endlessFxPlayer();
	endlessSetFxPlayer(endlessEconomyIndex());
	const int pct = endlessPerkAmmoPercent();
	if (pct == ammoLabelPct)
	{
		endlessSetFxPlayer(fxSaved);
		return;
	}
	ammoLabelPct = pct;

	for (int i = 1; i <= OPTION_NUM; ++i)
	{
		if (ammoBaseAmmo[i] == 0                        // charge/infinite sidekick: no magazine
		    || ammoBaseName[i][0] == '\0'               // empty slot
		    || strncmp(ammoBaseName[i], "None", 4) == 0)
			continue;

		char label[16];
		int label_len = snprintf(label, sizeof(label), "Ammo %d",
		                         endlessPerkSidekickAmmo(ammoBaseAmmo[i]));
		// Clamp snprintf's reported length to the bytes actually stored before unsigned arithmetic.
		if (label_len < 0)
			label_len = 0;
		else if (label_len > (int)sizeof(label) - 1)
			label_len = (int)sizeof(label) - 1;

		// Align to column 15 like the originals (or one space past the name if it already
		// reaches that far), clamped so the label never overruns the 30-char name field.
		size_t len = strlen(ammoBaseName[i]);
		size_t col = (len < 15) ? 15 : len + 1;
		if (col + (size_t)label_len > 30)
			col = (30 >= (size_t)label_len) ? 30 - (size_t)label_len : 0;
		if (len > col)
			len = col;

		memcpy(options[i].name, ammoBaseName[i], len);
		for (size_t k = len; k < col; ++k)
			options[i].name[k] = ' ';
		memcpy(options[i].name + col, label, label_len);
		options[i].name[col + label_len] = '\0';
	}

	endlessSetFxPlayer(fxSaved);
}

void JE_loadItemDat(void)
{
	FILE *f = NULL;
	
	// Custom containers always use the episode 4/5 embedded item-table layout.
	if (episodeNum <= 3 && !customEpisodeActive())
	{
		f = dir_fopen_die(data_dir(), "tyrian.hdt", "rb");
		fread_s32_die(&episode1DataLoc, 1, f);
		fseek(f, episode1DataLoc, SEEK_SET);
	}
	else
	{
		// episode 4 stores item data in the level file
		f = dir_fopen_die(JE_episodeDir(), levelFile, "rb");
		fseek(f, lvlPos[lvlNum-1], SEEK_SET);
	}

	JE_word itemNum[7]; /* [1..7] */
	fread_u16_die(itemNum, 7, f);

	const int weapons_bounds[2][2] = {{0, WEAP_END1}, {WEAP_START2, WEAP_NUM}};
	for (int bank = 0; bank < 2; ++bank)
	{
		for (int i = weapons_bounds[bank][0]; i < weapons_bounds[bank][1] + 1; ++i)
		{
			fread_u16_die(&weapons[i].drain,           1, f);
			fread_u8_die( &weapons[i].shotrepeat,      1, f);
			fread_u8_die( &weapons[i].multi,           1, f);
			fread_u16_die(&weapons[i].weapani,         1, f);
			fread_u8_die( &weapons[i].max,             1, f);
			fread_u8_die( &weapons[i].tx,              1, f);
			fread_u8_die( &weapons[i].ty,              1, f);
			fread_u8_die( &weapons[i].aim,             1, f);
			fread_u8_die(  weapons[i].attack,          8, f);
			fread_u8_die(  weapons[i].del,             8, f);
			fread_s8_die(  weapons[i].sx,              8, f);
			fread_s8_die(  weapons[i].sy,              8, f);
			fread_s8_die(  weapons[i].bx,              8, f);
			fread_s8_die(  weapons[i].by,              8, f);
			fread_u16_die( weapons[i].sg,              8, f);
			fread_s8_die( &weapons[i].acceleration,    1, f);
			fread_s8_die( &weapons[i].accelerationx,   1, f);
			fread_u8_die( &weapons[i].circlesize,      1, f);
			fread_u8_die( &weapons[i].sound,           1, f);
			fread_u8_die( &weapons[i].trail,           1, f);
			fread_u8_die( &weapons[i].shipblastfilter, 1, f);
		}
	}
	
	for (int i = 0; i < PORT_NUM + 1; ++i)
	{
		Uint8 nameLen;
		fread_u8_die( &nameLen,                   1, f);
		fread_die(    &weaponPort[i].name,    1, 30, f);
		weaponPort[i].name[MIN(nameLen, 30)] = '\0';
		fread_u8_die( &weaponPort[i].opnum,       1, f);
		fread_u16_die( weaponPort[i].op[0],      11, f);
		fread_u16_die( weaponPort[i].op[1],      11, f);
		fread_u16_die(&weaponPort[i].cost,        1, f);
		fread_u16_die(&weaponPort[i].itemgraphic, 1, f);
		fread_u16_die(&weaponPort[i].poweruse,    1, f);
	}

	for (int i = 0; i < SPECIAL_NUM + 1; ++i)
	{
		Uint8 nameLen;
		fread_u8_die( &nameLen,                1, f);
		fread_die(    &special[i].name,    1, 30, f);
		special[i].name[MIN(nameLen, 30)] = '\0';
		fread_u16_die(&special[i].itemgraphic, 1, f);
		fread_u8_die( &special[i].pwr,         1, f);
		fread_u8_die( &special[i].stype,       1, f);
		fread_u16_die(&special[i].wpn,         1, f);
	}

	for (int i = 0; i < POWER_NUM + 1; ++i)
	{
		Uint8 nameLen;
		fread_u8_die( &nameLen,                 1, f);
		fread_die(    &powerSys[i].name,    1, 30, f);
		powerSys[i].name[MIN(nameLen, 30)] = '\0';
		fread_u16_die(&powerSys[i].itemgraphic, 1, f);
		fread_u8_die( &powerSys[i].power,       1, f);
		fread_s8_die( &powerSys[i].speed,       1, f);
		fread_u16_die(&powerSys[i].cost,        1, f);
	}

	for (int i = 0; i < SHIP_NUM + 1; ++i)
	{
		Uint8 nameLen;
		fread_u8_die( &nameLen,                 1, f);
		fread_die(    &ships[i].name,       1, 30, f);
		ships[i].name[MIN(nameLen, 30)] = '\0';
		fread_u16_die(&ships[i].shipgraphic,    1, f);
		fread_u16_die(&ships[i].itemgraphic,    1, f);
		fread_u8_die( &ships[i].ani,            1, f);
		fread_s8_die( &ships[i].spd,            1, f);
		fread_u8_die( &ships[i].dmg,            1, f);
		fread_u16_die(&ships[i].cost,           1, f);
		fread_u8_die( &ships[i].bigshipgraphic, 1, f);
	}

	/* The data flies the Dragonwing only as the linked pair's rear half, through the shipgraphic 0
	 * draw path, so no episode's table carries a row for it. */
	strcpy(ships[SHIP_DRAGONWING].name, "Dragonwing");
	ships[SHIP_DRAGONWING].shipgraphic    = 0;
	ships[SHIP_DRAGONWING].itemgraphic    = 277;
	ships[SHIP_DRAGONWING].ani            = 2;
	ships[SHIP_DRAGONWING].spd            = 0;
	ships[SHIP_DRAGONWING].dmg            = 22;
	ships[SHIP_DRAGONWING].cost           = 17500;
	ships[SHIP_DRAGONWING].bigshipgraphic = 32;

	for (int i = 0; i < OPTION_NUM + 1; ++i)
	{
		Uint8 nameLen;
		fread_u8_die(  &nameLen,                1, f);
		fread_die(     &options[i].name,    1, 30, f);
		options[i].name[MIN(nameLen, 30)] = '\0';
		fread_u8_die(  &options[i].pwr,         1, f);
		fread_u16_die( &options[i].itemgraphic, 1, f);
		fread_u16_die( &options[i].cost,        1, f);
		fread_u8_die(  &options[i].tr,          1, f);
		fread_u8_die(  &options[i].option,      1, f);
		fread_s8_die(  &options[i].opspd,       1, f);
		fread_u8_die(  &options[i].ani,         1, f);
		fread_u16_die(  options[i].gr,         20, f);
		fread_u8_die(  &options[i].wport,       1, f);
		fread_u16_die( &options[i].wpnum,       1, f);
		fread_u8_die(  &options[i].ammo,        1, f);
		fread_bool_die(&options[i].stop,           f);
		fread_u8_die(  &options[i].icongr,      1, f);
	}

	for (int i = 0; i < SHIELD_NUM + 1; ++i)
	{
		Uint8 nameLen;
		fread_u8_die( &nameLen,                1, f);
		fread_die(    &shields[i].name,    1, 30, f);
		shields[i].name[MIN(nameLen, 30)] = '\0';
		fread_u8_die( &shields[i].tpwr,        1, f);
		fread_u8_die( &shields[i].mpwr,        1, f);
		fread_u16_die(&shields[i].itemgraphic, 1, f);
		fread_u16_die(&shields[i].cost,        1, f);
	}

	const int enemies_bounds[2][2] = {{0, ENEMY_END1}, {ENEMY_START2, ENEMY_NUM}};
	for (int bank = 0; bank < 2; ++bank)
	{
		for (int i = enemies_bounds[bank][0]; i < enemies_bounds[bank][1] + 1; ++i)
		{
			fread_u8_die( &enemyDat[i].ani,           1, f);
			fread_u8_die(  enemyDat[i].tur,           3, f);
			fread_u8_die(  enemyDat[i].freq,          3, f);
			fread_s8_die( &enemyDat[i].xmove,         1, f);
			fread_s8_die( &enemyDat[i].ymove,         1, f);
			fread_s8_die( &enemyDat[i].xaccel,        1, f);
			fread_s8_die( &enemyDat[i].yaccel,        1, f);
			fread_s8_die( &enemyDat[i].xcaccel,       1, f);
			fread_s8_die( &enemyDat[i].ycaccel,       1, f);
			fread_s16_die(&enemyDat[i].startx,        1, f);
			fread_s16_die(&enemyDat[i].starty,        1, f);
			fread_s8_die( &enemyDat[i].startxc,       1, f);
			fread_s8_die( &enemyDat[i].startyc,       1, f);
			fread_u8_die( &enemyDat[i].armor,         1, f);
			fread_u8_die( &enemyDat[i].esize,         1, f);
			fread_u16_die( enemyDat[i].egraphic,     20, f);
			fread_u8_die( &enemyDat[i].explosiontype, 1, f);
			fread_u8_die( &enemyDat[i].animate,       1, f);
			fread_u8_die( &enemyDat[i].shapebank,     1, f);
			fread_s8_die( &enemyDat[i].xrev,          1, f);
			fread_s8_die( &enemyDat[i].yrev,          1, f);
			fread_u16_die(&enemyDat[i].dgr,           1, f);
			fread_s8_die( &enemyDat[i].dlevel,        1, f);
			fread_s8_die( &enemyDat[i].dani,          1, f);
			fread_u8_die( &enemyDat[i].elaunchfreq,   1, f);
			fread_u16_die(&enemyDat[i].elaunchtype,   1, f);
			fread_s16_die(&enemyDat[i].value,         1, f);
			fread_u16_die(&enemyDat[i].eenemydie,     1, f);
		}
	}

	fclose(f);

	// Note the slot the cut DOS Charge-Laser Cannon takes and what it displaces, then add it if
	// the toggle is on (shops + debug menu); off leaves the stock item layout.
	JE_captureChargeLaserSlot();
	JE_applyChargeLaserCannon();

	// Capture the Zica Lv11 native pattern now, while wpn 209 still holds freshly-loaded
	// data, so ZICA_BASE_AUTO can restore it later (JE_applyZicaLaserConfig may reshape it).
	{
		const int wn11 = weaponPort[5].op[0][10];
		if (wn11 > 0 && wn11 <= WEAP_NUM)
		{
			memcpy(zicaNativeSx, weapons[wn11].sx, sizeof(zicaNativeSx));
			memcpy(zicaNativeBx, weapons[wn11].bx, sizeof(zicaNativeBx));
			zicaNativeCaptured = true;
		}
	}

	JE_applyZicaLaserConfig();      // shape the configured Lv11 pattern + build the long beams

	JE_applySuperSparks();          // restore the ep4/5 superspark trails on the ep1-3 weapons

	JE_applyEpDiffs();              // force the configured ep1-3/ep4-5 data on the other diff items

	// Replace Wobbley's stray first-frame sprite with its normal rest frame.
	for (int i = 0; i <= OPTION_NUM; ++i)
		if (strncmp(options[i].name, "Wobbley", 7) == 0 && options[i].gr[0] == 166)
			options[i].gr[0] = options[i].gr[1];

	// Blank Flying Punch's fifth tile, which spills into the next sprite's art.
	if (weapons[794].sg[0] == 663)
		weapons[794].sg[0] = 547;  // 47 through 54 are empty entries in the same sheet

	// Give every icon-less shop item a placeholder icon (167) so it no longer renders a
	// blank box; skip the "None" entries (their blank icon is intentional) and empty slots.
	for (int i = 1; i <= PORT_NUM; ++i)
		if (weaponPort[i].itemgraphic == 0 && weaponPort[i].name[0] && strncmp(weaponPort[i].name, "None", 4) != 0)
			weaponPort[i].itemgraphic = 167;
	for (int i = 1; i <= POWER_NUM; ++i)
		if (powerSys[i].itemgraphic == 0 && powerSys[i].name[0] && strncmp(powerSys[i].name, "None", 4) != 0)
			powerSys[i].itemgraphic = 167;
	for (int i = 1; i <= OPTION_NUM; ++i)
		if (options[i].itemgraphic == 0 && options[i].name[0] && strncmp(options[i].name, "None", 4) != 0)
			options[i].itemgraphic = 167;
	for (int i = 1; i <= SHIELD_NUM; ++i)
		if (shields[i].itemgraphic == 0 && shields[i].name[0] && strncmp(shields[i].name, "None", 4) != 0)
			shields[i].itemgraphic = 167;

	// Snapshot the shipped icons (placeholders included) and then, if the toggle is on, hand the
	// sheet's 11 unreferenced icons to the weapons that would otherwise share someone else's.
	JE_captureUnusedShopSprites();
	JE_applyUnusedShopSprites();

	customWeaponInit();             // claim a free port + compile the user's custom weapon

	// Last, so the snapshot sees every slot as the shops will: after the Charge-Laser and the
	// custom weapon have claimed theirs.
	JE_captureAmmoSidekickBases();  // snapshot the shipped sidekick names + magazine sizes...
	JE_labelAmmoSidekicks();        // ...and show the magazine size in the shop name (Flying Punch, Bubble Gum-Gun)
}

void JE_applyItemDataSettings(void)
{
	// These rewrites start from shipped constants, making the whole pass idempotent.
	JE_applyChargeLaserCannon();
	JE_applyZicaLaserConfig();
	JE_applySuperSparks();
	JE_applyEpDiffs();
	JE_applyUnusedShopSprites();
}

void JE_initEpisode(JE_byte newEpisode)
{
	// Only JE_initEpisodeCustom's inner call may retain custom mode.
	if (customEpisodeActive() && !customEpisodeActivating())
		customEpisodeDeactivate();  // Force a stock-file reload.

	if (newEpisode == episodeNum && !episodeForceReload)
	{
		// Same episode: the item data isn't reloaded, but the settings baked into it may have
		// changed in the menu since.
		JE_applyItemDataSettings();
		return;
	}

	episodeForceReload = false;
	episodeNum = newEpisode;

	if (customEpisodeActive())
	{
		// episodeNum remains the base used by episode-specific rules.
		snprintf(levelFile,    sizeof(levelFile),    "%s", CUSTOM_EP_LVL_NAME);
		snprintf(cube_file,    sizeof(cube_file),    "%s", CUSTOM_EP_CUBES_NAME);
		snprintf(episode_file, sizeof(episode_file), "%s", CUSTOM_EP_SCRIPT_NAME);
	}
	else
	{
		snprintf(levelFile,    sizeof(levelFile),    "tyrian%hhu.lvl",  episodeNum);
		snprintf(cube_file,    sizeof(cube_file),    "cubetxt%hhu.dat", episodeNum);
		snprintf(episode_file, sizeof(episode_file), "levels%hhu.dat",  episodeNum);
	}

	JE_analyzeLevel();
	JE_loadItemDat();
}

/* Extended episode IDs preserve custom identity in Endless state. */

int JE_currentEpisodeId(void)
{
	const int custom = customEpisodeIdFromLocal(customEpisodeCurrent());
	return custom >= 0 ? CUSTOM_EPISODE_ID_BASE + custom : episodeNum;
}

void JE_initEpisodeId(int id)
{
	if (id == JE_currentEpisodeId())
		return;

	if (id >= CUSTOM_EPISODE_ID_BASE)
	{
		const int local = customEpisodeIdToLocal(id - CUSTOM_EPISODE_ID_BASE);
		if (local < 0 || !JE_initEpisodeCustom(local))
		{
			// JE_loadMap handles any level number that is invalid in the fallback episode.
			fprintf(stderr, "custom episode: id %d is not installed; falling back to episode 1\n", id);
			JE_initEpisode(1);
		}
	}
	else
		JE_initEpisode((JE_byte)id);
}

void JE_scanForEpisodes(void)
{
	for (int i = 0; i < EPISODE_MAX; ++i)
	{
		char ep_file[20];
		snprintf(ep_file, sizeof(ep_file), "tyrian%d.lvl", i + 1);
		episodeAvail[i] = dir_file_exists(data_dir(), ep_file);
	}
}

unsigned int JE_findNextEpisode(void)
{
	unsigned int newEpisode = episodeNum;

	jumpBackToEpisode1 = false;

	if (customEpisodeActive())
	{
		// Custom episodes repeat after their credits instead of entering stock content.
		jumpBackToEpisode1 = true;
		gameHasRepeated = true;
		return episodeNum;
	}

	while (true)
	{
		newEpisode++;
		
		if (newEpisode > EPISODE_MAX)
		{
			newEpisode = 1;
			jumpBackToEpisode1 = true;
			gameHasRepeated = true;
		}
		
		if (episodeAvail[newEpisode-1] || newEpisode == episodeNum)
		{
			break;
		}
	}
	
	return newEpisode;
}
