/* Custom weapons designed in the Weapon Editor. See custom_weapon.h. */

#include "custom_weapon.h"

#include "config.h"
#include "config_file.h"  // COMPILE_TIME_ASSERT
#include "episodes.h"
#include "file.h"
#include "network.h"
#include "player.h"
#include "sprite.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

JE_WeaponType customWeaponRaw[CUSTOM_WEAPON_MODES][CUSTOM_POWER_LEVELS];

int  customWeaponModes = 1;

char customWeaponName[31]    = "Custom Weapon";
int  customWeaponCost        = 5000;
int  customWeaponPowerUse    = 3;
int  customWeaponEquipSlot   = CUSTOM_EQUIP_FRONT;
int  customWeaponItemGraphic = 0;    // 0 = borrow a default at init; else the shop icon
int  customWeaponChargeStages = 1;   // shot count (1 = no charging); option pwr = this - 1

int  customWeaponEditLevel = 0;
int  customWeaponEditMode  = 0;
bool customWeaponEnabled   = true;
int  customWeaponPort      = 0;
int  customSidekickSlot    = 0;

int  customWeaponOwnerPort[CUSTOM_WEAPON_OWNERS];
int  customSidekickOwnerSlot[CUSTOM_WEAPON_OWNERS];

// Owner 0 also backs solo play; supported online modes fill both owners.
static CustomWeaponSlot customWeaponOwnerDesign[CUSTOM_WEAPON_OWNERS];
static bool             customWeaponOwnerDefined[CUSTOM_WEAPON_OWNERS];

COMPILE_TIME_ASSERT(custom_scratch_slots_fit_the_gap,
                    CUSTOM_WEAP_BASE + CUSTOM_WEAPON_OWNERS * CUSTOM_WEAPON_MODES *
                    CUSTOM_POWER_LEVELS <= WEAP_START2);

// Sidekick body appearance (see custom_weapon.h). Defaults reproduce the previous hardcoded
// side pod (the cut Charge-Laser's known-good frame in spriteSheet9).
int  customSidekickMount     = 0;   // side pod
int  customSidekickSprite    = 87;  // Charge-Laser side-pod body frame
int  customSidekickFrames    = 1;   // static (no flip-book) by default
int  customSidekickFrameStep = 1;   // consecutive sprites when animated
int  customSidekickAnimate   = 1;   // animate while firing

CustomBulletPreset customBulletPreset[CUSTOM_BULLET_PRESET_MAX];
int                customBulletPresetCount = 0;

static CustomWeaponSlot customWeaponLib[CUSTOM_WEAPON_LIB_MAX];
int              customWeaponLibCount    = 0;
int              customWeaponCurrentSlot = 0;

// Most indexes in this file pass through these clamps; baseMulti must stay nonzero.
static inline int clampi(int v, int lo, int hi)
{
	return (v < lo) ? lo : (v > hi) ? hi : v;
}

// The scratch weapon slot backing (owner, mode, level).
static int customScratchSlot(int owner, int mode, int level)
{
	return CUSTOM_WEAP_BASE + (owner * CUSTOM_WEAPON_MODES + mode) * CUSTOM_POWER_LEVELS + level;
}

int customWeaponLocalOwner(void)
{
	const int owner = (int)gameplay_local_player_index();
	return clampi(owner, 0, CUSTOM_WEAPON_OWNERS - 1);
}

/* Simulation-side custom-port test for every owner. `customWeaponPort` names only the local
 * owner's port and differs between online peers. */
bool customWeaponPortIsCustom(JE_word port)
{
	for (int i = 0; i < CUSTOM_WEAPON_OWNERS; ++i)
		if (customWeaponOwnerPort[i] != 0 && customWeaponOwnerPort[i] == (int)port)
			return true;
	return false;
}

bool customSidekickSlotIsCustom(int option)
{
	for (int i = 0; i < CUSTOM_WEAPON_OWNERS; ++i)
		if (customSidekickOwnerSlot[i] != 0 && customSidekickOwnerSlot[i] == option)
			return true;
	return false;
}

// Copy the editable globals into / out of a design record. The library slots, the reserved
// per-owner slots and the wire format all go through these, so a design has one definition.
static void customDesignStore(CustomWeaponSlot *s)
{
	SDL_strlcpy(s->name, customWeaponName, sizeof(s->name));
	s->cost         = customWeaponCost;
	s->powerUse     = customWeaponPowerUse;
	s->equipSlot    = customWeaponEquipSlot;
	s->itemGraphic  = customWeaponItemGraphic;
	s->chargeStages = customWeaponChargeStages;
	s->modes        = customWeaponModes;
	s->sidekickMount     = customSidekickMount;
	s->sidekickSprite    = customSidekickSprite;
	s->sidekickFrames    = customSidekickFrames;
	s->sidekickFrameStep = customSidekickFrameStep;
	s->sidekickAnimate   = customSidekickAnimate;
	memcpy(s->raw, customWeaponRaw, sizeof(customWeaponRaw));
}

static void customDesignLoad(const CustomWeaponSlot *s)
{
	SDL_strlcpy(customWeaponName, s->name, sizeof(customWeaponName));
	customWeaponCost         = s->cost;
	customWeaponPowerUse     = s->powerUse;
	customWeaponEquipSlot    = s->equipSlot;
	customWeaponItemGraphic  = s->itemGraphic;
	customWeaponChargeStages = s->chargeStages;
	customWeaponModes        = clampi(s->modes, 1, CUSTOM_WEAPON_MODES);
	customSidekickMount      = s->sidekickMount;
	customSidekickSprite     = s->sidekickSprite;
	customSidekickFrames     = s->sidekickFrames;
	customSidekickFrameStep  = s->sidekickFrameStep;
	customSidekickAnimate    = s->sidekickAnimate;
	memcpy(customWeaponRaw, s->raw, sizeof(customWeaponRaw));
}

int customBulletMaxPower(int presetIdx)
{
	if (presetIdx < 0 || presetIdx >= customBulletPresetCount)
		return 1;
	return clampi(customBulletPreset[presetIdx].maxPower, 1, CUSTOM_POWER_LEVELS);
}

// Clamp imported, generated, and hand-edited designs to engine limits.
static void sanitizeRawWeapon(JE_WeaponType *w)
{
	w->multi = (JE_byte)clampi(w->multi, 1, CUSTOM_BULLETS_MAX);
	w->max   = (JE_byte)clampi(w->max,   1, CUSTOM_BULLETS_MAX);
	// shotrepeat 0 fires every tick. Stock lasers rely on it for a continuous beam.
	if (w->sound > CUSTOM_SOUND_MAX)
		w->sound = CUSTOM_SOUND_MAX;
}

// A blank one-bullet design (fires nothing until edited); what a fresh install
// starts with, matching a blank editor canvas.
static void makeBlankWeapon(JE_WeaponType *w)
{
	memset(w, 0, sizeof(*w));
	sanitizeRawWeapon(w);   // floors multi/max to 1
}

// A plain two-bullet upward blaster; what the editor's RESET actions restore.
static void makeDefaultWeapon(JE_WeaponType *w)
{
	memset(w, 0, sizeof(*w));
	w->shotrepeat = 8;
	w->multi      = 2;
	w->max        = 2;
	for (int i = 0; i < 2; ++i)
	{
		w->attack[i] = 12;
		w->del[i]    = 255;              // long life (avoid the 98..121 sentinels)
		w->sx[i]     = 0;
		w->sy[i]     = 11;              // positive = travels up
		w->bx[i]     = (i == 0) ? -4 : 4;
		w->by[i]     = 0;
		w->sg[i]     = 261;             // a plain bolt sprite
	}
	w->trail = 0xFF;   // none
	w->sound = 1;
}

// Small LCG so Randomize gives variety without depending on the game RNG state.
static int customRand(int range)
{
	static unsigned int seed = 2463534242u;
	seed = seed * 1103515245u + 12345u;
	return (range > 0) ? (int)((seed >> 16) % (unsigned)range) : 0;
}

// The number of fire modes a source port defines (1 or 2), clamped to what we support.
static int sourcePortModes(int port)
{
	int m = weaponPort[port].opnum;
	if (m < 1) m = 1;
	if (m > CUSTOM_WEAPON_MODES) m = CUSTOM_WEAPON_MODES;
	return m;
}

// Resolve an import source + fire mode + base power level to a concrete weapon number.
static int resolveSourceWeapon(int presetIdx, int mode, int basePower)
{
	if (presetIdx < 0 || presetIdx >= customBulletPresetCount)
		return 0;

	const CustomBulletPreset *bp = &customBulletPreset[presetIdx];
	if (bp->sourcePort > 0 && bp->sourcePort <= PORT_NUM)
	{
		const int m    = clampi(mode, 0, sourcePortModes(bp->sourcePort) - 1);
		const int maxp = clampi(bp->maxPower, 1, CUSTOM_POWER_LEVELS);
		const int lvl  = clampi(basePower, 1, maxp) - 1;   // 0-based op[] index
		int wn = weaponPort[bp->sourcePort].op[m][lvl];
		for (int p = lvl; wn <= 0 && p >= 0; --p)          // fall back to a lower defined level
			wn = weaponPort[bp->sourcePort].op[m][p];
		for (int p = lvl + 1; wn <= 0 && p < 11; ++p)      // ... or a higher one
			wn = weaponPort[bp->sourcePort].op[m][p];
		return wn;
	}

	// Charge sidekicks use wpnum + stage; maxPower is the number of stages.
	const int stages = clampi(bp->maxPower, 1, CUSTOM_POWER_LEVELS);
	return bp->sourceWeapon + clampi(basePower, 1, stages) - 1;
}

int customBulletPresetWeapon(int presetIdx, int mode, int basePower)
{
	const int wn = resolveSourceWeapon(presetIdx, mode, basePower);
	return (wn > 0 && wn <= WEAP_NUM && weapons[wn].sg[0] != 0) ? wn : 0;
}

void customWeaponImportLevel(int presetIdx, int basePower)
{
	const int m  = clampi(customWeaponEditMode,  0, CUSTOM_WEAPON_MODES - 1);
	const int p  = clampi(customWeaponEditLevel, 0, CUSTOM_POWER_LEVELS - 1);
	const int wn = resolveSourceWeapon(presetIdx, m, basePower);
	if (wn <= 0 || wn > WEAP_NUM || weapons[wn].sg[0] == 0)
		return;

	customWeaponRaw[m][p] = weapons[wn];   // byte-for-byte copy
	sanitizeRawWeapon(&customWeaponRaw[m][p]);
}

void customWeaponImportAllLevels(int presetIdx)
{
	if (presetIdx < 0 || presetIdx >= customBulletPresetCount)
		return;

	const CustomBulletPreset *bp = &customBulletPreset[presetIdx];
	if (bp->sourcePort > 0 && bp->sourcePort <= PORT_NUM)
	{
		const int modes = sourcePortModes(bp->sourcePort);
		const int maxp  = clampi(bp->maxPower, 1, CUSTOM_POWER_LEVELS);
		customWeaponModes = modes;
		for (int m = 0; m < modes; ++m)
			for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
			{
				const int lvl = (p < maxp) ? p : maxp - 1;   // clamp beyond the source's top level
				int wn = weaponPort[bp->sourcePort].op[m][lvl];
				for (int q = lvl; wn <= 0 && q >= 0; --q)     // fall back to a lower defined level
					wn = weaponPort[bp->sourcePort].op[m][q];
				if (wn > 0 && wn <= WEAP_NUM && weapons[wn].sg[0] != 0)
				{
					customWeaponRaw[m][p] = weapons[wn];
					sanitizeRawWeapon(&customWeaponRaw[m][p]);
				}
			}
		customWeaponPowerUse = clampi(weaponPort[bp->sourcePort].poweruse, 0, 255);
		if (customWeaponEditMode >= customWeaponModes)
			customWeaponEditMode = 0;
	}
	else
	{
		// Copy consecutive sidekick charge shots into matching power levels.
		// Levels beyond the source's last stage repeat its top stage.
		const int wn     = bp->sourceWeapon;                              // stage-0 weapon
		const int stages = clampi(bp->maxPower, 1, CUSTOM_POWER_LEVELS);  // charge-shot count
		customWeaponModes    = 1;
		customWeaponEditMode = 0;

		int lastValid = -1;
		for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
		{
			const int stageWn = (p < stages) ? (wn + p) : -1;
			if (stageWn > 0 && stageWn <= WEAP_NUM && weapons[stageWn].sg[0] != 0)
			{
				customWeaponRaw[0][p] = weapons[stageWn];
				sanitizeRawWeapon(&customWeaponRaw[0][p]);
				lastValid = p;
			}
			else if (lastValid >= 0)
			{
				customWeaponRaw[0][p] = customWeaponRaw[0][lastValid];  // repeat the top stage
			}
		}

		// Adopt the real charge-shot count (1 = a non-charge sidekick). If it actually
		// charges, equip it as a sidekick since that's the only slot charging works in.
		customWeaponChargeStages = clampi(lastValid + 1, 1, CUSTOM_POWER_LEVELS);
		if (customWeaponChargeStages >= 2 &&
		    customWeaponEquipSlot != CUSTOM_EQUIP_LEFT &&
		    customWeaponEquipSlot != CUSTOM_EQUIP_RIGHT &&
		    customWeaponEquipSlot != CUSTOM_EQUIP_BOTH)
		{
			customWeaponEquipSlot = CUSTOM_EQUIP_LEFT;
		}

		// Clone the source sidekick body, mount, and animation along with its shot.
		if (bp->sourceOption > 0 && bp->sourceOption <= OPTION_NUM)
		{
			const JE_OptionType *so = &options[bp->sourceOption];
			customSidekickMount     = clampi(so->tr, 0, CUSTOM_SIDEKICK_MOUNTS - 1);
			customSidekickSprite    = clampi(so->gr[0], 1, 65535);   // body sprite is 1-based
			customSidekickFrames    = clampi(so->ani, 1, 20);
			customSidekickFrameStep = (so->ani > 1) ? clampi((int)so->gr[1] - (int)so->gr[0], 0, 40) : 1;
			customSidekickAnimate   = clampi(so->option, 1, 2);
		}
	}

	// Adopt the source's name; this is a full editable clone.
	strncpy(customWeaponName, bp->name, sizeof(customWeaponName) - 1);
	customWeaponName[sizeof(customWeaponName) - 1] = '\0';
}

// Append bullet segments without changing dst's volley-wide fields.
static void combineWeaponInto(JE_WeaponType *dst, const JE_WeaponType *src)
{
	int n = clampi(dst->multi, 1, CUSTOM_BULLETS_MAX);
	const int add = clampi(src->multi, 1, CUSTOM_BULLETS_MAX);
	for (int i = 0; i < add && n < CUSTOM_BULLETS_MAX; ++i, ++n)
	{
		dst->sg[n]     = src->sg[i];
		dst->attack[n] = src->attack[i];
		dst->del[n]    = src->del[i];
		dst->sx[n]     = src->sx[i];
		dst->sy[n]     = src->sy[i];
		dst->bx[n]     = src->bx[i];
		dst->by[n]     = src->by[i];
	}
	dst->multi = (JE_byte)n;
	if (dst->max < dst->multi)   // fire all the combined bullets together each shot
		dst->max = dst->multi;
}

void customWeaponAddLevel(int presetIdx, int basePower)
{
	const int m  = clampi(customWeaponEditMode,  0, CUSTOM_WEAPON_MODES - 1);
	const int p  = clampi(customWeaponEditLevel, 0, CUSTOM_POWER_LEVELS - 1);
	const int wn = resolveSourceWeapon(presetIdx, m, basePower);
	if (wn <= 0 || wn > WEAP_NUM || weapons[wn].sg[0] == 0)
		return;

	combineWeaponInto(&customWeaponRaw[m][p], &weapons[wn]);
	sanitizeRawWeapon(&customWeaponRaw[m][p]);
}

void customWeaponAddAllLevels(int presetIdx)
{
	if (presetIdx < 0 || presetIdx >= customBulletPresetCount)
		return;

	const CustomBulletPreset *bp = &customBulletPreset[presetIdx];
	const int modes = clampi(customWeaponModes, 1, CUSTOM_WEAPON_MODES);

	if (bp->sourcePort > 0 && bp->sourcePort <= PORT_NUM)
	{
		// Add the source port's whole power curve: each of its power levels is combined onto the
		// matching custom level (levels past the source's top repeat its top level).
		const int smodes = sourcePortModes(bp->sourcePort);
		const int maxp   = clampi(bp->maxPower, 1, CUSTOM_POWER_LEVELS);
		for (int m = 0; m < modes; ++m)
		{
			const int sm = clampi(m, 0, smodes - 1);   // reuse the source's last mode if it has fewer
			OT_ASSUME(sm >= 0);                        // sourcePortModes returns >= 1, so smodes-1 >= 0
			for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
			{
				const int lvl = (p < maxp) ? p : maxp - 1;   // clamp beyond the source's top level
				OT_ASSUME(lvl >= 0);                         // clampi bounds maxp to [1, LEVELS]
				int wn = weaponPort[bp->sourcePort].op[sm][lvl];
				for (int q = lvl; wn <= 0 && q >= 0; --q)     // fall back to a lower defined level
					wn = weaponPort[bp->sourcePort].op[sm][q];
				if (wn > 0 && wn <= WEAP_NUM && weapons[wn].sg[0] != 0)
				{
					combineWeaponInto(&customWeaponRaw[m][p], &weapons[wn]);
					sanitizeRawWeapon(&customWeaponRaw[m][p]);
				}
			}
		}
	}
	else
	{
		// Repeat the top charge stage through the remaining custom power levels.
		const int wn     = bp->sourceWeapon;
		const int stages = clampi(bp->maxPower, 1, CUSTOM_POWER_LEVELS);
		int lastValidWn = 0;
		for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
		{
			int stageWn = (p < stages) ? (wn + p) : 0;
			if (stageWn > 0 && stageWn <= WEAP_NUM && weapons[stageWn].sg[0] != 0)
				lastValidWn = stageWn;
			else
				stageWn = lastValidWn;   // repeat the top valid stage
			if (stageWn > 0 && stageWn <= WEAP_NUM && weapons[stageWn].sg[0] != 0)
			{
				combineWeaponInto(&customWeaponRaw[0][p], &weapons[stageWn]);
				sanitizeRawWeapon(&customWeaponRaw[0][p]);
			}
		}
	}
}

void customWeaponReset(void)
{
	const int m = clampi(customWeaponEditMode,  0, CUSTOM_WEAPON_MODES - 1);
	const int p = clampi(customWeaponEditLevel, 0, CUSTOM_POWER_LEVELS - 1);
	makeDefaultWeapon(&customWeaponRaw[m][p]);
}

void customWeaponResetAllLevels(void)
{
	for (int m = 0; m < CUSTOM_WEAPON_MODES; ++m)
		for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
			makeDefaultWeapon(&customWeaponRaw[m][p]);
	strcpy(customWeaponName, "Custom Weapon");
	customWeaponCost         = 5000;
	customWeaponPowerUse     = 3;
	customWeaponEquipSlot    = CUSTOM_EQUIP_FRONT;
	customWeaponItemGraphic  = clampi(weaponPort[1].itemgraphic, 1, 237);
	customWeaponChargeStages = 1;
	customWeaponModes        = 1;
	customWeaponEditMode     = 0;
	customSidekickMount      = 0;
	customSidekickSprite     = 87;
	customSidekickFrames     = 1;
	customSidekickFrameStep  = 1;
	customSidekickAnimate    = 1;
}

void customWeaponCopyToAllLevels(void)
{
	// Copy the level currently being edited into every level of the current mode.
	const int m   = clampi(customWeaponEditMode,  0, CUSTOM_WEAPON_MODES - 1);
	const int src = clampi(customWeaponEditLevel, 0, CUSTOM_POWER_LEVELS - 1);
	for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
		if (p != src)
			customWeaponRaw[m][p] = customWeaponRaw[m][src];
}

typedef struct
{
	JE_word     sg;
	JE_byte     attack, del;
	JE_shortint sx, sy, bx, by;
} AutoScaleBullet;

enum
{
	AUTO_SCALE_TILES = 8,
	AUTO_SCALE_UNITS = 96,
	AUTO_SCALE_FRAMES = 16,
	AUTO_SCALE_VOLLEY = 8,
	AUTO_SCALE_VOLLEY_WIDE = 16,
	AUTO_SCALE_VOLLEY_CHAIN = 12,
	AUTO_SCALE_REACH_PX = 40,
	AUTO_SCALE_GAP_PX = 8,
	AUTO_SCALE_BEAM_GAP_PX = 10,
};

enum
{
	AUTO_SCALE_CORE,
	AUTO_SCALE_RING,
	AUTO_SCALE_SOLO,
	AUTO_SCALE_FLANK,
};

typedef struct
{
	AutoScaleBullet tile[AUTO_SCALE_TILES];
	int count, frame, kind, side, ring, grown, pair, origin;
	double weight;
} AutoScaleUnit;

typedef struct
{
	AutoScaleUnit unit[AUTO_SCALE_UNITS];
	int count, frames, cx2, csx, fwd, stride, axis2;
	double reach;
} AutoScaleShape;

typedef struct
{
	int left, right, frame, side, ring, stepX, stepY, room;
	double lean, turn;
} AutoScaleSeed;

enum
{
	AUTO_SCALE_HELD = 384,
	AUTO_SCALE_MATCH = 24,
	AUTO_SCALE_LADDER = 24,
};

typedef struct
{
	int origin, tile;
	AutoScaleBullet b;
} AutoScaleHeld;

static const double AUTO_SCALE_DEGREE = 0.017453292519943295;

static AutoScaleShape autoScaleAnchor, autoScaleStock, autoScaleLevel, autoScaleTrial;
static AutoScaleHeld autoScaleHeld[AUTO_SCALE_HELD];
static int autoScaleHeldCount;
static int autoScaleStyle = CUSTOM_SPREAD_ARROW;

int customWeaponSpread = CUSTOM_SPREAD_AUTO;
int customWeaponLastSpread = CUSTOM_SPREAD_AUTO;

static const char *const customSpreadNames[CUSTOM_SPREAD_COUNT] =
{
	"Auto", "Stock", "Arrow", "Fan", "Wall", "Crescent", "Converge", "Wave", "Twin", "Burst",
	"Nova", "Lob", "Spiral", "Weave"
};

const char *customWeaponSpreadName(int spread)
{
	return customSpreadNames[clampi(spread, 0, CUSTOM_SPREAD_COUNT - 1)];
}

static AutoScaleBullet autoScaleGet(const JE_WeaponType *w, int i)
{
	const AutoScaleBullet b = { w->sg[i], w->attack[i], w->del[i], w->sx[i], w->sy[i], w->bx[i], w->by[i] };
	return b;
}

static void autoScalePut(JE_WeaponType *w, int i, const AutoScaleBullet *b)
{
	w->sg[i] = b->sg;
	w->attack[i] = b->attack;
	w->del[i] = b->del;
	w->sx[i] = b->sx;
	w->sy[i] = b->sy;
	w->bx[i] = b->bx;
	w->by[i] = b->by;
}

static int autoScaleRound(double v)
{
	return (int)((v < 0.0) ? v - 0.5 : v + 0.5);
}

static bool autoScaleSteerable(const AutoScaleBullet *b)
{
	return abs(b->sx) <= 30 && abs(b->sy) <= 30 && (b->sx != 0 || b->sy != 0);
}

static bool autoScaleAimable(const AutoScaleBullet *b)
{
	return autoScaleSteerable(b) || (b->sx > 100 && b->sy != 0 && abs(b->sy) <= 30);
}

static int autoScaleDriftX(const AutoScaleBullet *b, int csx)
{
	return (b->sx > 100) ? csx : b->sx;
}

static int autoScaleTileDamage(JE_byte attack)
{
	if (attack >= 1 && attack <= 98)
		return attack;
	return (attack >= 251) ? attack - 250 : 0;
}

static JE_byte autoScaleWithDamage(JE_byte attack, int damage)
{
	if (attack >= 1 && attack <= 98)
		return (JE_byte)clampi(damage, 1, 98);
	if (attack >= 251)
		return (JE_byte)(250 + clampi(damage, 1, 5));
	return attack;
}

static int autoScaleChainDamage(JE_byte attack, int depth)
{
	if (attack < 101 || attack > 249)
		return 0;
	int dmg = 1;
	if (depth > 0 && attack - 100 <= WEAP_NUM)
	{
		const JE_WeaponType *const w = &weapons[attack - 100];
		for (int i = 0; i < clampi(w->multi, 1, CUSTOM_BULLETS_MAX); ++i)
			dmg +=
			    autoScaleTileDamage(w->attack[i]) + autoScaleChainDamage(w->attack[i], depth - 1);
	}
	return dmg;
}

static double autoScaleWeaponDps(const JE_WeaponType *w)
{
	const int multi = clampi(w->multi, 1, CUSTOM_BULLETS_MAX);
	const int slots = MAX(multi, clampi(w->max, 1, CUSTOM_BULLETS_MAX));
	int dmg = 0;
	for (int i = 0; i < slots; ++i)
		dmg += autoScaleTileDamage(w->attack[i]) + autoScaleChainDamage(w->attack[i], 2);
	return (double)dmg * multi / slots * 35.0 / (w->shotrepeat + 1);
}

static bool autoScaleSpriteRef(JE_word sg, Sprite2_array *sheet, unsigned int *index,
                               JE_word *offset)
{
	if (sg == 0 || sg >= 60000)
		return false;
	const JE_word high = (sg > 1000) ? (JE_word)(sg - sg % 1000) : 0;
	const JE_word base = (JE_word)(sg - high);
	*sheet = (base > 500) ? spriteSheet12 : spriteSheet8;
	*index = (base > 500) ? base - 500u : base;
	*offset = (JE_word)(high + ((base > 500) ? 500 : 0));
	return *index >= 1;
}

static int autoScaleSpriteArea(Sprite2_array sheet, unsigned int index)
{
	int x0, y0, x1, y1;
	if (!sprite2_ink_bounds(sheet, index, &x0, &y0, &x1, &y1))
		return 0;
	return (x1 - x0 + 1) * (y1 - y0 + 1);
}

static int autoScaleProfile(Sprite2_array sheet, unsigned int index)
{
	int x0, y0, x1, y1;
	if (!sprite2_ink_bounds(sheet, index, &x0, &y0, &x1, &y1))
		return -1;
	return (y0 == 0) | (y1 >= 13) << 1 | (x0 == 0) << 2 | (x1 >= 11) << 3;
}

static JE_word autoScaleMirrorSprite(JE_word sg)
{
	static const int nearby[] = { 1, -1, 19, -19, 2, -2, 38, -38, 18, -18, 20, -20 };
	Sprite2_array sheet;
	unsigned int index;
	JE_word offset;
	if (!autoScaleSpriteRef(sg, &sheet, &index, &offset) ||
	    autoScaleSpriteArea(sheet, index) == 0 || sprite2_hflip_equal(sheet, index, index))
		return sg;

	for (size_t i = 0; i < COUNTOF(nearby); ++i)
	{
		const int candidate = (int)index + nearby[i];
		if (candidate >= 1 && sprite2_hflip_equal(sheet, index, (unsigned int)candidate))
			return (JE_word)(offset + candidate);
	}
	return sg;
}

static JE_word autoScaleFamilyStep(JE_word sg, int step)
{
	Sprite2_array sheet;
	unsigned int index;
	JE_word offset;
	if (step == 0 || !autoScaleSpriteRef(sg, &sheet, &index, &offset) ||
	    autoScaleSpriteArea(sheet, index) == 0 || autoScaleMirrorSprite(sg) != sg)
		return sg;

	const Uint8 bank = sprite2_dominant_bank(sheet, index);
	const int profile = autoScaleProfile(sheet, index);
	unsigned int lo = index, hi = index;
	while (lo > 1 && index - lo < 5 && autoScaleSpriteArea(sheet, lo - 1) > 0 &&
	       sprite2_dominant_bank(sheet, lo - 1) == bank)
		--lo;
	while (hi - index < 5 && autoScaleSpriteArea(sheet, hi + 1) > 0 &&
	       sprite2_dominant_bank(sheet, hi + 1) == bank)
		++hi;

	unsigned int pick = index;
	int pickArea = autoScaleSpriteArea(sheet, index);
	for (int s = 0; s < abs(step); ++s)
	{
		unsigned int next = pick;
		int nextArea = -1;
		for (unsigned int i = lo; i <= hi; ++i)
		{
			const int area = autoScaleSpriteArea(sheet, i);
			if ((step > 0) ? area <= pickArea : area >= pickArea)
				continue;
			if (nextArea >= 0 && ((step > 0) ? area >= nextArea : area <= nextArea))
				continue;
			if ((unsigned int)autoScaleMirrorSprite((JE_word)(offset + i)) != offset + i ||
			    autoScaleProfile(sheet, i) != profile)
				continue;
			next = i;
			nextArea = area;
		}
		if (nextArea < 0)
			break;
		pick = next;
		pickArea = nextArea;
	}
	return (JE_word)(offset + pick);
}

static int autoScaleOverhang(JE_word sg)
{
	if (sg <= 60000 || (unsigned int)(sg - 60001) >= COUNTOF(sprite_table[0].sprite))
		return 0;
	const int width = get_sprite_width(OPTION_SHAPES, sg - 60001);
	return (width > 0) ? width - 12 : 0;
}

static int autoScaleSway2(JE_byte circlesize)
{
	const int size = (circlesize > 19) ? circlesize % 20 : circlesize;
	if (size == 0)
		return 0;
	int dev = size >> 1, dir = 1, x = 0, lo = 0, hi = 0;
	for (int tick = 0; tick < 8 * size + 8; ++tick)
	{
		dev += dir;
		x += dev;
		if (abs(dev) == size)
			dir = -dir;
		lo = MIN(lo, x);
		hi = MAX(hi, x);
	}
	return lo + hi;
}

static int autoScaleUnitX2(const AutoScaleUnit *u)
{
	int lo = u->tile[0].bx, hi = lo + autoScaleOverhang(u->tile[0].sg);
	for (int i = 1; i < u->count; ++i)
	{
		lo = MIN(lo, u->tile[i].bx);
		hi = MAX(hi, u->tile[i].bx + autoScaleOverhang(u->tile[i].sg));
	}
	return lo + hi;
}

static int autoScaleUnitY2(const AutoScaleUnit *u)
{
	int lo = u->tile[0].by, hi = lo;
	for (int i = 1; i < u->count; ++i)
	{
		lo = MIN(lo, u->tile[i].by);
		hi = MAX(hi, u->tile[i].by);
	}
	return lo + hi;
}

static int autoScaleUnitWidth(const AutoScaleUnit *u)
{
	int lo = 127, hi = -128;
	for (int i = 0; i < u->count; ++i)
	{
		const AutoScaleBullet *const t = &u->tile[i];
		Sprite2_array sheet;
		unsigned int index;
		JE_word offset;
		int x0 = 0, y0, x1 = 11, y1;
		if (t->sg > 60000 && (unsigned int)(t->sg - 60001) < COUNTOF(sprite_table[0].sprite))
			x1 = MAX(get_sprite_width(OPTION_SHAPES, t->sg - 60001), 1) - 1;
		else if (autoScaleSpriteRef(t->sg, &sheet, &index, &offset) &&
		         !sprite2_ink_bounds(sheet, index, &x0, &y0, &x1, &y1))
		{
			x0 = 0;
			x1 = 11;
		}
		lo = MIN(lo, t->bx + x0);
		hi = MAX(hi, t->bx + x1);
	}
	return hi - lo + 1;
}

static void autoScaleUnitShift(AutoScaleUnit *u, int dx, int dy)
{
	for (int i = 0; i < u->count; ++i)
	{
		u->tile[i].bx = (JE_shortint)clampi(u->tile[i].bx + dx, -128, 127);
		u->tile[i].by = (JE_shortint)clampi(u->tile[i].by + dy, -128, 127);
	}
}

static bool autoScaleUnitWide(const AutoScaleUnit *u)
{
	const int x2 = autoScaleUnitX2(u);
	bool wide = false;
	for (int i = 0; i < u->count; ++i)
	{
		const int rel = 2 * u->tile[i].bx + autoScaleOverhang(u->tile[i].sg) - x2;
		bool mirrored = false;
		for (int j = 0; j < u->count && !mirrored; ++j)
			mirrored = 2 * u->tile[j].bx + autoScaleOverhang(u->tile[j].sg) - x2 == -rel &&
			           u->tile[j].by == u->tile[i].by;
		if (!mirrored)
			return false;
		wide |= rel != 0;
	}
	return wide;
}

static bool autoScaleUnitPlain(const AutoScaleUnit *u)
{
	if (autoScaleUnitWide(u))
		return true;
	for (int i = 0; i < u->count; ++i)
		if (autoScaleMirrorSprite(u->tile[i].sg) != u->tile[i].sg)
			return false;
	return true;
}

static bool autoScaleUnitColumn(const AutoScaleUnit *u)
{
	if (u->count < 2)
		return false;
	for (int i = 0; i < u->count; ++i)
		if (abs(u->tile[i].bx - u->tile[0].bx) > 1 || u->tile[i].sg == 0 || u->tile[i].sg >= 60000)
			return false;
	return true;
}

static void autoScaleUnitMirror(const AutoScaleUnit *u, int cx2, int csx, AutoScaleUnit *out)
{
	*out = *u;
	bool translate = autoScaleUnitWide(u);
	for (int i = 0; i < u->count && !translate; ++i)
		translate = u->tile[i].sg > 60000;
	const int shift = cx2 - autoScaleUnitX2(u);
	for (int i = 0; i < out->count; ++i)
	{
		AutoScaleBullet *const t = &out->tile[i];
		if (translate)
		{
			t->bx = (JE_shortint)clampi(t->bx + shift, -128, 127);
		}
		else
		{
			t->bx = (JE_shortint)clampi(cx2 - t->bx - autoScaleOverhang(t->sg), -128, 127);
			t->sg = autoScaleMirrorSprite(t->sg);
		}
		if (abs(t->sx) <= 100)
			t->sx = (JE_shortint)clampi(2 * csx - t->sx, -100, 100);
	}
	out->side = -u->side;
}

static void autoScaleTileOrder(const AutoScaleUnit *u, int *order)
{
	for (int i = 0; i < u->count; ++i)
	{
		int j = i;
		for (; j > 0; --j)
		{
			const AutoScaleBullet *const prev = &u->tile[order[j - 1]];
			if (prev->by < u->tile[i].by ||
			    (prev->by == u->tile[i].by && prev->bx <= u->tile[i].bx))
				break;
			order[j] = order[j - 1];
		}
		order[j] = i;
	}
}

static void autoScaleAdoptSprites(AutoScaleUnit *mirrored, const AutoScaleUnit *actual)
{
	int mine[AUTO_SCALE_TILES], theirs[AUTO_SCALE_TILES];
	autoScaleTileOrder(mirrored, mine);
	autoScaleTileOrder(actual, theirs);
	for (int i = 0; i < mirrored->count && i < actual->count; ++i)
		mirrored->tile[mine[i]].sg = actual->tile[theirs[i]].sg;
}

static double autoScaleLean(const AutoScaleUnit *u, int side, int csx, int fwd)
{
	const AutoScaleBullet *const t = &u->tile[0];
	if (!autoScaleAimable(t))
		return 0.0;
	const int sx = autoScaleDriftX(t, csx);
	return atan2((double)((side < 0) ? csx - sx : sx - csx), (double)(fwd * t->sy));
}

static void autoScaleTurn(AutoScaleUnit *u, int side, int csx, int fwd, double lean)
{
	if (!autoScaleAimable(&u->tile[0]))
		return;
	for (int i = 0; i < u->count; ++i)
	{
		AutoScaleBullet *const t = &u->tile[i];
		const double vx = autoScaleDriftX(t, csx) - csx, vy = t->sy;
		const double speed = sqrt(vx * vx + vy * vy);
		const int sx = clampi(autoScaleRound(csx + side * speed * sin(lean)), -30, 30);
		const int sy = clampi(autoScaleRound(fwd * speed * cos(lean)), -30, 30);
		if (sx == 0 && sy == 0)
			continue;
		t->sx = (JE_shortint)sx;
		t->sy = (JE_shortint)sy;
	}
}

static void autoScaleSpin(AutoScaleUnit *u, double angle)
{
	if (!autoScaleAimable(&u->tile[0]))
		return;
	for (int i = 0; i < u->count; ++i)
	{
		AutoScaleBullet *const t = &u->tile[i];
		const int drift = autoScaleDriftX(t, 0);
		const double speed = sqrt((double)drift * drift + (double)t->sy * t->sy);
		const double heading = atan2((double)drift, (double)t->sy) + angle;
		const int sx = clampi(autoScaleRound(speed * sin(heading)), -30, 30);
		const int sy = clampi(autoScaleRound(speed * cos(heading)), -30, 30);
		if (sx == 0 && sy == 0)
			continue;
		t->sx = (JE_shortint)sx;
		t->sy = (JE_shortint)sy;
	}
}

static bool autoScaleAbuts(JE_word left, JE_word right)
{
	Sprite2_array sheet;
	unsigned int index;
	JE_word offset;
	if (!autoScaleSpriteRef(left, &sheet, &index, &offset))
		return false;
	const int leftEdges = autoScaleProfile(sheet, index);
	if (!autoScaleSpriteRef(right, &sheet, &index, &offset))
		return false;
	const int rightEdges = autoScaleProfile(sheet, index);
	return leftEdges >= 0 && rightEdges >= 0 && (leftEdges & 8) != 0 && (rightEdges & 4) != 0;
}

static bool autoScaleTilesJoin(const AutoScaleBullet *a, const AutoScaleBullet *b, int stride)
{
	if (a->sx > 100 && a->sx == b->sx && abs(a->sy - b->sy) == 1 && a->del == b->del &&
	    a->sg != 0 && b->sg != 0 && a->sg < 60000 && b->sg < 60000 && abs(a->bx - b->bx) <= 1 &&
	    (a->by == b->by || abs(a->by - b->by) == 14))
		return true;
	if (a->sx != b->sx || a->sy != b->sy || a->del != b->del || a->sg == 0 || b->sg == 0)
		return false;
	const bool optionShape = a->sg >= 60000;
	if (optionShape != (b->sg >= 60000) || (!optionShape && a->sg / 500 != b->sg / 500))
		return false;

	const AutoScaleBullet *const top = (a->by <= b->by) ? a : b, *const low = (top == a) ? b : a;
	const AutoScaleBullet *const left = (a->bx <= b->bx) ? a : b;
	const AutoScaleBullet *const right = (left == a) ? b : a;
	const int dx = right->bx - left->bx, dy = low->by - top->by;
	if (optionShape)
		return dx <= 1 && dy >= 8 && dy <= 32 && low->sg == top->sg + 1;
	if (dx <= 1 && dy == 14)
	{
		const int ds = (int)low->sg - (int)top->sg;
		return ds == 0 || (ds >= 1 && ds <= 2 * stride) || ds == 19 || ds == 38;
	}
	if (dx == 12 && dy <= 1)
	{
		const int ds = (int)right->sg - (int)left->sg;
		return ((ds >= 1 && ds <= 2 * stride) || ds == 19) && autoScaleAbuts(left->sg, right->sg);
	}
	return false;
}

static int autoScaleGcd(int a, int b)
{
	while (b != 0)
	{
		const int t = a % b;
		a = b;
		b = t;
	}
	return a;
}

static int autoScaleRoot(const int *parent, int i)
{
	while (parent[i] != i)
		i = parent[i];
	return i;
}

static int autoScaleUnitLateral(const AutoScaleUnit *u, int cx2, int csx)
{
	const int sx = u->tile[0].sx;
	return abs(autoScaleUnitX2(u) - cx2) + ((abs(sx) <= 100) ? 8 * abs(sx - csx) : 0);
}

static int autoScalePairCost(const AutoScaleUnit *l, const AutoScaleUnit *r, int cx2, int csx)
{
	const AutoScaleBullet *const a = &l->tile[0], *const b = &r->tile[0];
	const int sxWant = (abs(a->sx) <= 100) ? 2 * csx - a->sx : a->sx;
	return abs(autoScaleUnitX2(r) - (2 * cx2 - autoScaleUnitX2(l))) +
	       abs(autoScaleUnitY2(r) - autoScaleUnitY2(l)) + 8 * abs(b->sx - sxWant) +
	       8 * abs(b->sy - a->sy);
}

static int autoScaleFrameTiles(const AutoScaleShape *s, int f)
{
	int n = 0;
	for (int i = 0; i < s->count; ++i)
		if (s->unit[i].frame == f)
			n += s->unit[i].count;
	return n;
}

static int autoScaleFrameRings(const AutoScaleShape *s, int f)
{
	int r = 0;
	for (int i = 0; i < s->count; ++i)
		if (s->unit[i].frame == f && s->unit[i].kind == AUTO_SCALE_RING)
			r = MAX(r, s->unit[i].ring);
	return r;
}

static int autoScaleFindRing(const AutoScaleShape *s, int f, int ring, int side)
{
	for (int i = 0; i < s->count; ++i)
	{
		const AutoScaleUnit *const u = &s->unit[i];
		if (u->frame == f && u->kind == AUTO_SCALE_RING && u->ring == ring && u->side == side)
			return i;
	}
	return -1;
}

static int autoScaleMate(const AutoScaleShape *s, int i)
{
	for (int j = 0; j < s->count; ++j)
		if (j != i && s->unit[j].pair == s->unit[i].pair && s->unit[i].pair >= 0)
			return j;
	return -1;
}

static int autoScaleMainUnit(const AutoScaleShape *s, int f)
{
	int best = -1;
	for (int i = 0; i < s->count; ++i)
	{
		const AutoScaleUnit *const u = &s->unit[i];
		if (u->frame != f || u->kind != AUTO_SCALE_CORE || u->count != 1 || u->tile[0].sg == 0 ||
		    u->tile[0].sg >= 60000)
			continue;
		if (best < 0 || abs(autoScaleUnitY2(u)) < abs(autoScaleUnitY2(&s->unit[best])))
			best = i;
	}
	return best;
}

static int autoScaleCoreUnit(const AutoScaleShape *s, int f)
{
	int best = -1;
	for (int i = 0; i < s->count; ++i)
	{
		const AutoScaleUnit *const u = &s->unit[i];
		if (u->frame != f || u->kind != AUTO_SCALE_CORE)
			continue;
		if (best < 0 || u->count > s->unit[best].count ||
		    (u->count == s->unit[best].count &&
		     abs(autoScaleUnitY2(u)) < abs(autoScaleUnitY2(&s->unit[best]))))
			best = i;
	}
	return best;
}

static int autoScaleShapeSide(const AutoScaleShape *s)
{
	int side = 0;
	for (int i = 0; i < s->count; ++i)
	{
		const int x2 = autoScaleUnitX2(&s->unit[i]);
		const int clear = MAX(16, autoScaleUnitWidth(&s->unit[i]));
		const int spawn = (x2 <= -clear) ? -1 : (x2 >= clear) ? 1 : 0;
		const int seen = (x2 - s->axis2 <= -clear) ? -1 : (x2 - s->axis2 >= clear) ? 1 : 0;
		if (spawn == 0 || seen != spawn || (side != 0 && spawn != side))
			return 0;
		side = spawn;
	}
	return side;
}

static void autoScaleCentre(AutoScaleShape *s)
{
	const int dx = (s->cx2 - s->axis2) / 2;
	if (dx == 0 || autoScaleShapeSide(s) != 0)
		return;
	for (int i = 0; i < s->count; ++i)
		autoScaleUnitShift(&s->unit[i], -dx, 0);
	s->cx2 -= 2 * dx;
}

static bool autoScaleAddUnit(AutoScaleShape *s, const AutoScaleUnit *u)
{
	if (s->count == AUTO_SCALE_UNITS)
		return false;
	s->unit[s->count++] = *u;
	return true;
}

static bool autoScaleDecompose(const JE_WeaponType *w, AutoScaleShape *shape)
{
	static AutoScaleBullet bullet[CUSTOM_BULLETS_MAX];
	static AutoScaleUnit pool[AUTO_SCALE_UNITS];
	static int parent[CUSTOM_BULLETS_MAX];
	int lateral[AUTO_SCALE_UNITS], order[AUTO_SCALE_UNITS], partner[AUTO_SCALE_UNITS];
	bool taken[AUTO_SCALE_UNITS];
	const int multi = clampi(w->multi, 1, CUSTOM_BULLETS_MAX);
	const int max = clampi(w->max, 1, CUSTOM_BULLETS_MAX);
	const int period = (max > multi) ? max : multi;
	const int cycle = (max > multi) ? multi / autoScaleGcd(multi, max) * max : multi;
	if (cycle > CUSTOM_BULLETS_MAX || cycle / multi > AUTO_SCALE_FRAMES)
		return false;

	shape->frames = cycle / multi;
	shape->stride = clampi(w->weapani + 1, 1, 8);
	shape->count = 0;
	int units = 0;
	for (int f = 0; f < shape->frames; ++f)
	{
		for (int i = 0; i < multi; ++i)
		{
			bullet[i] = autoScaleGet(w, (f * multi + i) % period);
			parent[i] = i;
		}
		for (int i = 0; i < multi; ++i)
			for (int j = i + 1; j < multi; ++j)
				if (autoScaleTilesJoin(&bullet[i], &bullet[j], shape->stride))
				{
					const int ri = autoScaleRoot(parent, i), rj = autoScaleRoot(parent, j);
					parent[MAX(ri, rj)] = MIN(ri, rj);
				}
		for (int i = 0; i < multi; ++i)
		{
			if (parent[i] != i)
				continue;
			int members = 0;
			for (int j = i; j < multi; ++j)
				members += autoScaleRoot(parent, j) == i;
			bool opened = false;
			for (int j = i; j < multi; ++j)
			{
				if (autoScaleRoot(parent, j) != i)
					continue;
				if (!opened || members > AUTO_SCALE_TILES)
				{
					if (units == AUTO_SCALE_UNITS)
						return false;
					const AutoScaleUnit fresh = {.frame = f, .ring = 0, .pair = -1, .weight = 1.0};
					pool[units++] = fresh;
					opened = true;
				}
				AutoScaleUnit *const u = &pool[units - 1];
				u->tile[u->count++] = bullet[j];
			}
		}
	}

	int x2Sum = 0, sxSum = 0, sxCount = 0, sySum = 0;
	for (int u = 0; u < units; ++u)
	{
		const AutoScaleBullet *const t = &pool[u].tile[0];
		x2Sum += autoScaleUnitX2(&pool[u]);
		if (abs(t->sx) <= 100)
		{
			sxSum += t->sx;
			++sxCount;
		}
		if (abs(t->sy) <= 30)
			sySum += t->sy;
	}
	shape->cx2 = autoScaleRound((double)x2Sum / units);
	shape->axis2 = -autoScaleSway2(w->circlesize);
	shape->csx = (sxCount > 0) ? autoScaleRound((double)sxSum / sxCount) : 0;
	shape->fwd = (sySum < 0) ? -1 : 1;
	shape->reach = 0.0;

	for (int u = 0; u < units; ++u)
	{
		const AutoScaleBullet *const t = &pool[u].tile[0];
		const int dx2 = autoScaleUnitX2(&pool[u]) - shape->cx2;
		const int dv = (abs(t->sx) <= 100) ? t->sx - shape->csx : 0;
		lateral[u] = dx2 + 8 * dv;
		if (lateral[u] == 0)
			lateral[u] = (dv != 0) ? dv : ((dx2 < 0) ? -1 : 1);
		taken[u] = abs(dx2) <= 2 && dv == 0;
		partner[u] = -1;
		if (taken[u])
		{
			pool[u].kind = AUTO_SCALE_CORE;
			pool[u].side = 0;
			if (!autoScaleAddUnit(shape, &pool[u]))
				return false;
		}
	}

	int pairs = 0;
	for (int u = 0; u < units; ++u)
	{
		if (taken[u] || lateral[u] > 0)
			continue;
		int best = -1, bestCost = 25;
		for (int v = 0; v < units; ++v)
		{
			if (taken[v] || lateral[v] < 0 || pool[v].frame != pool[u].frame ||
			    pool[v].count != pool[u].count)
				continue;
			const int cost = autoScalePairCost(&pool[u], &pool[v], shape->cx2, shape->csx);
			if (cost < bestCost)
			{
				best = v;
				bestCost = cost;
			}
		}
		if (best >= 0)
		{
			taken[u] = taken[best] = true;
			partner[u] = best;
			order[pairs++] = u;
		}
	}
	for (int u = 0; u < units; ++u)
		if (!taken[u] && shape->frames == 1)
		{
			taken[u] = true;
			order[pairs++] = u;
		}

	for (int k = 1; k < pairs; ++k)
	{
		const int key = order[k];
		int j = k;
		for (; j > 0 && (pool[order[j - 1]].frame > pool[key].frame ||
		                 (pool[order[j - 1]].frame == pool[key].frame &&
		                  abs(lateral[order[j - 1]]) > abs(lateral[key])));
		     --j)
			order[j] = order[j - 1];
		order[j] = key;
	}

	for (int k = 0; k < pairs; ++k)
	{
		const int u = order[k];
		const int f = pool[u].frame;
		AutoScaleUnit left = pool[u], right;
		if (lateral[u] > 0)
		{
			right = pool[u];
			right.side = 1;
			autoScaleUnitMirror(&right, shape->cx2, shape->csx, &left);
		}
		else
		{
			left.side = -1;
			autoScaleUnitMirror(&left, shape->cx2, shape->csx, &right);
			if (partner[u] >= 0)
				autoScaleAdoptSprites(&right, &pool[partner[u]]);
		}
		left.kind = right.kind = AUTO_SCALE_RING;
		left.side = -1;
		right.side = 1;
		left.ring = right.ring = autoScaleFrameRings(shape, f) + 1;
		left.pair = right.pair = k;
		if (!autoScaleAddUnit(shape, &left) || !autoScaleAddUnit(shape, &right))
			return false;
		shape->reach = MAX(shape->reach, autoScaleLean(&left, -1, shape->csx, shape->fwd));
	}

	for (int u = 0; u < units; ++u)
	{
		if (taken[u])
			continue;
		AutoScaleUnit solo = pool[u];
		solo.kind = AUTO_SCALE_SOLO;
		solo.side = (lateral[u] < 0) ? -1 : 1;
		if (!autoScaleAddUnit(shape, &solo))
			return false;
		shape->reach = MAX(shape->reach, autoScaleLean(&solo, solo.side, shape->csx, shape->fwd));
	}

	autoScaleCentre(shape);
	int tiles = 0;
	for (int f = 0; f < shape->frames; ++f)
		tiles += autoScaleFrameTiles(shape, f);
	for (int i = 0; i < shape->count; ++i)
		shape->unit[i].origin = i;
	return tiles <= CUSTOM_BULLETS_MAX;
}

static double autoScaleFrameLateral(const AutoScaleShape *s, int f)
{
	int best = -1;
	for (int i = 0; i < s->count; ++i)
	{
		const AutoScaleUnit *const u = &s->unit[i];
		if (u->frame != f)
			continue;
		const int lat = (u->kind == AUTO_SCALE_CORE) ? 0 : autoScaleUnitLateral(u, s->cx2, s->csx);
		if (best < 0 || lat < best)
			best = lat;
	}
	return MAX(best, 0);
}

static bool autoScaleCollapse(AutoScaleUnit *u, const AutoScaleShape *s)
{
	if (!autoScaleUnitPlain(u))
		return false;
	for (int i = 0; i < u->count; ++i)
		if (abs(u->tile[i].sx) <= 100 && u->tile[i].sx != s->csx &&
		    !autoScaleSteerable(&u->tile[i]))
			return false;

	autoScaleUnitShift(u, (s->cx2 - autoScaleUnitX2(u)) / 2, 0);
	if (autoScaleSteerable(&u->tile[0]))
	{
		const double vx = u->tile[0].sx - s->csx, vy = u->tile[0].sy;
		const int sy = clampi(autoScaleRound(sqrt(vx * vx + vy * vy)), 1, 30) * s->fwd;
		for (int i = 0; i < u->count; ++i)
		{
			u->tile[i].sx = (JE_shortint)s->csx;
			u->tile[i].sy = (JE_shortint)sy;
		}
	}
	u->kind = AUTO_SCALE_CORE;
	u->side = 0;
	u->ring = 0;
	u->pair = -1;
	return true;
}

static bool autoScaleFramesEven(const AutoScaleShape *s)
{
	if (s->frames < 1)
		return false;
	const int tiles = autoScaleFrameTiles(s, 0);
	for (int f = 1; f < s->frames; ++f)
		if (autoScaleFrameTiles(s, f) != tiles)
			return false;
	return tiles >= 1;
}

static bool autoScaleFramesSame(const AutoScaleShape *s, int f, int g)
{
	static AutoScaleBullet a[CUSTOM_BULLETS_MAX], b[CUSTOM_BULLETS_MAX];
	int na = 0, nb = 0;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
		{
			if (s->unit[i].frame == f && na < CUSTOM_BULLETS_MAX)
				a[na++] = s->unit[i].tile[k];
			else if (s->unit[i].frame == g && nb < CUSTOM_BULLETS_MAX)
				b[nb++] = s->unit[i].tile[k];
		}
	if (na != nb)
		return false;
	for (int i = 0; i < na; ++i)
	{
		bool found = false;
		for (int j = 0; j < nb && !found; ++j)
			found = memcmp(&a[i], &b[j], sizeof(a[i])) == 0;
		if (!found)
			return false;
	}
	return true;
}

static bool autoScaleShrink(const AutoScaleShape *src, double share, AutoScaleShape *out)
{
	double frameLat[AUTO_SCALE_FRAMES], levels[AUTO_SCALE_FRAMES];
	int levelCount = 0;
	for (int f = 0; f < src->frames; ++f)
	{
		frameLat[f] = autoScaleFrameLateral(src, f);
		int j = levelCount;
		bool seen = false;
		for (int k = 0; k < levelCount; ++k)
			seen |= fabs(levels[k] - frameLat[f]) < 0.5;
		if (seen)
			continue;
		for (; j > 0 && levels[j - 1] > frameLat[f]; --j)
			levels[j] = levels[j - 1];
		levels[j] = frameLat[f];
		++levelCount;
	}
	const double cut = levels[(int)((levelCount - 1) * share + 0.5)];

	*out = *src;
	out->count = 0;
	out->frames = 0;
	for (int f = 0; f < src->frames; ++f)
	{
		if (frameLat[f] > cut + 0.5)
			continue;
		const int nf = out->frames++;
		const int rings = autoScaleFrameRings(src, f);
		const int keep = (int)(rings * share + 0.5);
		bool anchored = false;
		for (int i = 0; i < src->count; ++i)
		{
			AutoScaleUnit u = src->unit[i];
			if (u.frame != f || (u.kind == AUTO_SCALE_RING && u.ring > keep))
				continue;
			anchored |= u.kind != AUTO_SCALE_RING || keep > 0;
			u.frame = nf;
			if (!autoScaleAddUnit(out, &u))
				return false;
		}
		if (anchored || rings == 0)
			continue;

		AutoScaleUnit core = src->unit[autoScaleFindRing(src, f, 1, -1)];
		if (autoScaleCollapse(&core, src))
		{
			core.frame = nf;
			if (!autoScaleAddUnit(out, &core))
				return false;
			continue;
		}
		for (int side = -1; side <= 1; side += 2)
		{
			AutoScaleUnit u = src->unit[autoScaleFindRing(src, f, 1, side)];
			u.frame = nf;
			if (!autoScaleAddUnit(out, &u))
				return false;
		}
	}

	if (share <= 0.0 && cut > 0.5)
	{
		for (int f = 0; f < out->frames; ++f)
		{
			int only = -1, units = 0;
			for (int i = 0; i < out->count; ++i)
				if (out->unit[i].frame == f)
				{
					only = i;
					++units;
				}
			if (units == 1 && out->unit[only].kind == AUTO_SCALE_SOLO)
				autoScaleCollapse(&out->unit[only], out);
		}
	}

	bool same = out->frames > 1;
	for (int f = 1; f < out->frames && same; ++f)
		same = autoScaleFramesSame(out, 0, f);
	if (same)
	{
		int n = 0;
		for (int i = 0; i < out->count; ++i)
			if (out->unit[i].frame == 0)
				out->unit[n++] = out->unit[i];
		out->count = n;
		out->frames = 1;
	}
	return autoScaleFramesEven(out);
}

static bool autoScaleCoincides(const AutoScaleShape *s, const AutoScaleUnit *u)
{
	for (int i = 0; i < s->count; ++i)
	{
		const AutoScaleBullet *const a = &s->unit[i].tile[0], *const b = &u->tile[0];
		if (s->unit[i].frame == u->frame && a->bx == b->bx && a->by == b->by && a->sx == b->sx &&
		    a->sy == b->sy)
			return true;
	}
	return false;
}

static int autoScaleGap(const AutoScaleUnit *u)
{
	const int gap = (u->tile[0].sx > 100) ? AUTO_SCALE_BEAM_GAP_PX : AUTO_SCALE_GAP_PX;
	return MAX(autoScaleUnitWidth(u) + 3, gap);
}

static void autoScaleStyleSeed(AutoScaleSeed *s, const AutoScaleUnit *t, int fwd)
{
	const int gap = autoScaleGap(t);
	const bool steer = autoScaleAimable(&t->tile[0]);
	switch (autoScaleStyle)
	{
	case CUSTOM_SPREAD_FAN:
		s->stepX = gap;
		s->stepY = 0;
		s->turn = steer ? MAX(s->turn, 9.0 * AUTO_SCALE_DEGREE) : 0.0;
		break;
	case CUSTOM_SPREAD_WALL:
		s->stepX = MIN(MAX(s->stepX, gap + 4), MAX(gap, s->room / 2));
		s->stepY = 0;
		s->turn = 0.0;
		break;
	case CUSTOM_SPREAD_CRESCENT:
		s->stepX = MAX(s->stepX, gap);
		s->stepY = steer ? -3 * fwd : 0;
		s->turn = 0.0;
		break;
	case CUSTOM_SPREAD_CONVERGE:
		s->stepX = MIN(MAX(s->stepX, gap + 3), MAX(gap, s->room / 2));
		s->stepY = steer ? 2 * fwd : 0;
		s->turn = 0.0;
		break;
	case CUSTOM_SPREAD_WAVE:
		s->stepX = MAX(s->stepX, gap);
		s->stepY = steer ? 5 * fwd : 0;
		s->turn = 0.0;
		break;
	case CUSTOM_SPREAD_TWIN:
		s->stepX = MAX(s->stepX, gap);
		s->stepY = steer ? 2 * fwd : 0;
		s->turn = 0.0;
		break;
	case CUSTOM_SPREAD_BURST:
		s->stepX = 0;
		s->stepY = 0;
		s->turn = steer ? 14.0 * AUTO_SCALE_DEGREE : 0.0;
		break;
	case CUSTOM_SPREAD_NOVA:
		s->stepX = 0;
		s->stepY = 0;
		s->turn = steer ? 24.0 * AUTO_SCALE_DEGREE : 0.0;
		break;
	case CUSTOM_SPREAD_LOB:
		s->stepX = MAX(s->stepX, gap);
		s->stepY = 0;
		s->turn = 0.0;
		break;
	default:
		if (s->turn < 3.0 * AUTO_SCALE_DEGREE)
		{
			s->stepX = MAX(s->stepX, gap);
			if (s->stepY == 0 && steer)
				s->stepY = 3 * fwd;
		}
		break;
	}
}

static int autoScaleSeeds(const AutoScaleShape *src, AutoScaleSeed *seed)
{
	int n = 0;
	for (int f = 0; f < src->frames; ++f)
	{
		const int rings = autoScaleFrameRings(src, f);
		const int core = autoScaleCoreUnit(src, f);
		if (rings > 0)
		{
			AutoScaleSeed *const s = &seed[n++];
			s->frame = f;
			s->side = 0;
			s->ring = rings;
			s->left = autoScaleFindRing(src, f, rings, -1);
			s->right = autoScaleFindRing(src, f, rings, 1);
			const AutoScaleUnit *const t = &src->unit[s->left];
			const int prevIdx = (rings > 1) ? autoScaleFindRing(src, f, rings - 1, -1) : core;
			const AutoScaleUnit *const prev = (prevIdx >= 0) ? &src->unit[prevIdx] : NULL;
			const bool prevRing = prev != NULL && prev->kind == AUTO_SCALE_RING;
			const int prevX2 = prevRing ? autoScaleUnitX2(prev) : src->cx2;
			const int prevY2 = (prev != NULL) ? autoScaleUnitY2(prev) : autoScaleUnitY2(t);
			s->lean = autoScaleLean(t, -1, src->csx, src->fwd);
			const double fan =
			    s->lean - (prevRing ? autoScaleLean(prev, -1, src->csx, src->fwd) : 0.0);
			s->turn = (fan > 0.0) ? MIN(fan, 20.0 * AUTO_SCALE_DEGREE) : 0.0;
			s->stepX = clampi((prevX2 - autoScaleUnitX2(t)) / 2, 0, 24);
			s->stepY = clampi((autoScaleUnitY2(t) - prevY2) / 2, -8, 8);
			s->room = AUTO_SCALE_REACH_PX - abs(autoScaleUnitX2(t) - src->cx2) / 2;
			autoScaleStyleSeed(s, t, src->fwd);
		}
		else if (core >= 0)
		{
			AutoScaleSeed *const s = &seed[n++];
			s->frame = f;
			s->side = 0;
			s->ring = 0;
			s->left = core;
			s->right = -1;
			s->lean = 0.0;
			s->turn = autoScaleAimable(&src->unit[core].tile[0]) ? 7.0 * AUTO_SCALE_DEGREE : 0.0;
			s->stepX = autoScaleGap(&src->unit[core]);
			s->stepY = 3 * src->fwd;
			s->room = AUTO_SCALE_REACH_PX;
			autoScaleStyleSeed(s, &src->unit[core], src->fwd);
		}
		for (int i = 0; i < src->count; ++i)
		{
			const AutoScaleUnit *const u = &src->unit[i];
			if (u->frame != f || u->kind != AUTO_SCALE_SOLO)
				continue;
			AutoScaleSeed *const s = &seed[n++];
			s->frame = f;
			s->side = u->side;
			s->ring = 0;
			s->left = i;
			s->right = -1;
			s->lean = autoScaleLean(u, u->side, src->csx, src->fwd);
			s->turn = autoScaleAimable(&u->tile[0]) ? 7.0 * AUTO_SCALE_DEGREE : 0.0;
			s->stepX = autoScaleGap(u);
			s->stepY = 3 * src->fwd;
			s->room = AUTO_SCALE_REACH_PX - abs(autoScaleUnitX2(u) - src->cx2) / 2;
			autoScaleStyleSeed(s, u, src->fwd);
		}
	}
	return n;
}

static void autoScaleRetime(AutoScaleUnit *u, double fraction)
{
	if (!autoScaleAimable(&u->tile[0]) || u->tile[0].sy == 0)
		return;
	for (int i = 0; i < u->count; ++i)
	{
		const int speed = abs(u->tile[i].sy);
		if (speed == 0)
			continue;
		const int retimed = clampi(speed - autoScaleRound(speed * fraction), (speed + 1) / 2, 30);
		u->tile[i].sy = (JE_shortint)((u->tile[i].sy > 0) ? retimed : -retimed);
	}
}

static void autoScaleRingOffset(const AutoScaleSeed *s, double at, int *dx, int *dy)
{
	double x = at * s->stepX, y = at * s->stepY;
	switch (autoScaleStyle)
	{
	case CUSTOM_SPREAD_ARROW:
	case CUSTOM_SPREAD_CRESCENT:
	case CUSTOM_SPREAD_CONVERGE:
	case CUSTOM_SPREAD_SPIRAL:
	case CUSTOM_SPREAD_WEAVE:
		x += 2.0 * MAX(at - 1.0, 0.0);
		break;
	case CUSTOM_SPREAD_WAVE:
		x += 2.0 * MAX(at - 1.0, 0.0);
		y = ((int)ceil(at) % 2 == 1) ? s->stepY : 0.0;
		break;
	case CUSTOM_SPREAD_TWIN:
	{
		const double first =
		    clampi(autoScaleRound(2.5 * s->stepX), s->stepX, MAX(s->room - s->stepX, s->stepX));
		x = (at <= 1.0) ? at * first : first + (at - 1.0) * s->stepX;
		break;
	}
	default:
		break;
	}
	*dx = autoScaleRound(x);
	*dy = autoScaleRound(y);
}

static void autoScaleLob(AutoScaleUnit *u, double at, int side)
{
	if (!autoScaleAimable(&u->tile[0]) || u->tile[0].sy == 0)
		return;
	const int lead = abs(u->tile[0].sy);
	const int back = clampi(autoScaleRound(lead * (0.3 + 0.1 * at)), 2, 12);
	const int push = side * clampi(autoScaleRound(1.0 + at), 1, 6);
	const bool ahead = u->tile[0].sy > 0;
	for (int i = 0; i < u->count; ++i)
	{
		AutoScaleBullet *const t = &u->tile[i];
		const int lag = clampi(back + lead - abs(t->sy), 2, 12);
		t->sx = (JE_shortint)clampi(autoScaleDriftX(t, 0) + push, -30, 30);
		t->sy = (JE_shortint)(ahead ? -lag : lag);
	}
}

static void autoScaleRingMotion(AutoScaleUnit *u, double at, int side, int fwd)
{
	const int outward = side * fwd;
	switch (autoScaleStyle)
	{
	case CUSTOM_SPREAD_ARROW:
	case CUSTOM_SPREAD_WEAVE:
		autoScaleRetime(u, 0.05 * at);
		if (at >= 3.0)
			autoScaleSpin(u, outward * (at - 2.0) * 6.0 * AUTO_SCALE_DEGREE);
		break;
	case CUSTOM_SPREAD_LOB:
		autoScaleLob(u, at, side);
		break;
	case CUSTOM_SPREAD_CRESCENT:
		autoScaleRetime(u, -0.05 * at);
		break;
	case CUSTOM_SPREAD_CONVERGE:
		autoScaleSpin(u, -outward * at * 4.0 * AUTO_SCALE_DEGREE);
		break;
	default:
		break;
	}
}

static bool autoScaleGrowRing(const AutoScaleShape *src, const AutoScaleSeed *s, int j, bool half,
                              int cap, double leanCap, int pairId, int origin, AutoScaleShape *out)
{
	const double at = half ? j - 0.5 : j;
	const AutoScaleUnit *const t = &src->unit[s->left];
	if (autoScaleFrameTiles(out, s->frame) + 2 * t->count > cap)
		return false;
	if (s->side != 0)
	{
		AutoScaleUnit wing[2];
		for (int w = 0; w < 2; ++w)
		{
			const int side = (w == 0) ? -1 : 1;
			int dx, dy;
			autoScaleRingOffset(s, j, &dx, &dy);
			wing[w] = *t;
			autoScaleUnitShift(&wing[w], side * dx, dy);
			if (abs(autoScaleUnitX2(&wing[w]) - src->cx2) / 2 > AUTO_SCALE_REACH_PX)
				return false;
			if (s->turn > 0.0)
				autoScaleSpin(&wing[w], side * src->fwd * j * s->turn);
			else
				autoScaleRingMotion(&wing[w], j, side, src->fwd);
			wing[w].weight = t->weight * pow(0.75, j);
			wing[w].grown = j;
			wing[w].ring = j;
			wing[w].side = side;
			wing[w].kind = AUTO_SCALE_FLANK;
			wing[w].pair = pairId;
			wing[w].origin = origin + w;
			if (autoScaleCoincides(out, &wing[w]))
				return false;
		}
		return autoScaleAddUnit(out, &wing[0]) && autoScaleAddUnit(out, &wing[1]);
	}

	int dx, dy;
	autoScaleRingOffset(s, at, &dx, &dy);
	AutoScaleUnit u = *t;
	autoScaleUnitShift(&u, -dx, dy);
	if (abs(autoScaleUnitX2(&u) - src->cx2) / 2 > AUTO_SCALE_REACH_PX)
		return false;
	if (s->turn > 0.0)
		autoScaleTurn(&u, -1, src->csx, src->fwd, MIN(s->lean + at * s->turn, leanCap));
	else
		autoScaleRingMotion(&u, at, -1, src->fwd);
	u.weight = t->weight * pow(0.75, half ? j - 1 : j);
	u.grown = half ? j - 1 : j;
	u.ring = s->ring + j;
	u.side = -1;
	u.kind = AUTO_SCALE_RING;
	u.pair = pairId;
	u.origin = origin;
	if (autoScaleCoincides(out, &u) || !autoScaleAddUnit(out, &u))
		return false;

	AutoScaleUnit r;
	autoScaleUnitMirror(&u, src->cx2, src->csx, &r);
	if (s->right >= 0)
		autoScaleAdoptSprites(&r, &src->unit[s->right]);
	r.side = 1;
	r.origin = origin + 1;
	return !autoScaleCoincides(out, &r) && autoScaleAddUnit(out, &r);
}

static bool autoScaleFlank(const AutoScaleShape *src, AutoScaleShape *out)
{
	const int side = autoScaleShapeSide(src);
	if (side == 0)
		return false;

	for (int i = 0; i < src->count; ++i)
	{
		AutoScaleUnit u;
		autoScaleUnitMirror(&src->unit[i], src->axis2, 0, &u);
		u.kind = AUTO_SCALE_SOLO;
		u.side = -side;
		u.ring = 0;
		u.grown = 0;
		u.pair = -1;
		u.origin = 1000 + i;
		if (autoScaleCoincides(out, &u) || !autoScaleAddUnit(out, &u))
			return false;
	}
	return autoScaleFramesEven(out);
}

static bool autoScaleLattice(const AutoScaleShape *s)
{
	for (int i = 0; i < s->count; ++i)
		if (s->unit[i].kind != AUTO_SCALE_CORE)
			return false;
	return s->count > 0;
}

static bool autoScaleGrow(const AutoScaleShape *src, int extra, AutoScaleShape *out)
{
	static AutoScaleSeed seed[AUTO_SCALE_UNITS];
	*out = *src;
	if (extra > 0 && autoScaleFlank(src, out))
		return true;
	*out = *src;
	const bool half = autoScaleLattice(src) && extra % 2 == 1;
	if (autoScaleLattice(src))
		extra = (extra + 1) / 2;
	const int seeds = autoScaleSeeds(src, seed);
	const double leanCap = (autoScaleStyle == CUSTOM_SPREAD_NOVA)
	                     ? 120.0 * AUTO_SCALE_DEGREE
	                     : MIN(MAX(60.0 * AUTO_SCALE_DEGREE, src->reach + 30.0 * AUTO_SCALE_DEGREE),
	                           150.0 * AUTO_SCALE_DEGREE);
	int wide = 0, cap = AUTO_SCALE_VOLLEY;
	for (int i = 0; i < src->count; ++i)
	{
		wide |= src->unit[i].count > 1;
		for (int k = 0; k < src->unit[i].count; ++k)
			if (src->unit[i].tile[k].attack >= 101 && src->unit[i].tile[k].attack <= 249)
				cap = MAX(cap, AUTO_SCALE_VOLLEY_CHAIN);
	}
	if (wide)
		cap = MAX(cap, AUTO_SCALE_VOLLEY_WIDE);
	cap = MAX(cap, autoScaleFrameTiles(src, 0));

	int pairId = 1000;
	for (int j = 1; j <= extra; ++j)
	{
		const int before = out->count;
		bool fits = seeds > 0;
		for (int k = 0; k < seeds && fits; ++k)
			fits = autoScaleGrowRing(src, &seed[k], j, half, cap, leanCap, pairId++,
			                         100 + 8 * k + 2 * (j - 1), out);
		if (fits)
			fits = autoScaleFramesEven(out);
		if (!fits)
		{
			out->count = before;
			break;
		}
	}
	if (half)
	{
		int n = 0;
		for (int i = 0; i < out->count; ++i)
			if (out->unit[i].kind != AUTO_SCALE_CORE)
				out->unit[n++] = out->unit[i];
		if (n == 0)
			return false;
		out->count = n;
	}
	return autoScaleFramesEven(out);
}

static int autoScaleShiftArea(const AutoScaleUnit *u, int d)
{
	int total = 0;
	for (int i = 0; i < u->count; ++i)
	{
		Sprite2_array sheet;
		unsigned int index;
		JE_word offset;
		if (!autoScaleSpriteRef(u->tile[i].sg, &sheet, &index, &offset) || (int)index + d < 1)
			return -1;
		const unsigned int moved = (unsigned int)((int)index + d);
		const int area = autoScaleSpriteArea(sheet, moved);
		if (area == 0 ||
		    sprite2_dominant_bank(sheet, moved) != sprite2_dominant_bank(sheet, index) ||
		    autoScaleProfile(sheet, moved) != autoScaleProfile(sheet, index))
			return -1;
		total += area;
	}
	return total;
}

static void autoScaleColumnStep(AutoScaleUnit *u, int step)
{
	if (step == 0 || !autoScaleUnitColumn(u))
		return;

	int areaAt[13];
	for (int d = -6; d <= 6; ++d)
		areaAt[d + 6] = autoScaleShiftArea(u, d);

	int pick = 0;
	for (int s = 0; s < abs(step); ++s)
	{
		int next = pick;
		for (int d = -6; d <= 6; ++d)
		{
			const int area = areaAt[d + 6];
			if (area < 0 || ((step > 0) ? area <= areaAt[pick + 6] : area >= areaAt[pick + 6]))
				continue;
			if (next != pick && ((step > 0) ? area >= areaAt[next + 6] : area <= areaAt[next + 6]))
				continue;
			next = d;
		}
		if (next == pick)
			break;
		pick = next;
	}
	for (int i = 0; i < u->count; ++i)
		u->tile[i].sg = (JE_word)(u->tile[i].sg + pick);
}

static JE_word autoScaleBase(JE_word sg)
{
	return (sg >= 60000) ? sg : (JE_word)(sg % 1000);
}

static bool autoScaleChain(JE_byte attack)
{
	return attack >= 101 && attack <= 249;
}

static int autoScaleSpriteMass(JE_word sg)
{
	Sprite2_array sheet;
	unsigned int index;
	JE_word offset;
	if (autoScaleSpriteRef(sg, &sheet, &index, &offset))
		return autoScaleSpriteArea(sheet, index);
	if (sg > 60000 && (unsigned int)(sg - 60001) < COUNTOF(sprite_table[0].sprite))
		return get_sprite_width(OPTION_SHAPES, sg - 60001) *
		       get_sprite_height(OPTION_SHAPES, sg - 60001);
	return 0;
}

static int autoScaleSlots(const JE_WeaponType *w)
{
	return MAX(clampi(w->multi, 1, CUSTOM_BULLETS_MAX), clampi(w->max, 1, CUSTOM_BULLETS_MAX));
}

static AutoScaleHeld *autoScaleHeldFind(int origin, int tile)
{
	for (int i = 0; i < autoScaleHeldCount; ++i)
		if (autoScaleHeld[i].origin == origin && autoScaleHeld[i].tile == tile)
			return &autoScaleHeld[i];
	return NULL;
}

static int autoScaleUnitMass(const AutoScaleUnit *u)
{
	int mass = 0;
	for (int k = 0; k < u->count; ++k)
		mass += autoScaleSpriteMass(u->tile[k].sg);
	return mass;
}

static int autoScaleUnitPower(const AutoScaleUnit *u)
{
	int burst = 0;
	for (int k = 0; k < u->count; ++k)
		burst += autoScaleChainDamage(u->tile[k].attack, 2);
	return burst;
}

static void autoScaleHold(const AutoScaleShape *s)
{
	for (int i = 0; i < s->count; ++i)
		for (int h = 0; h < autoScaleHeldCount; ++h)
			if (autoScaleHeld[h].origin == s->unit[i].origin &&
			    autoScaleHeld[h].tile >= s->unit[i].count)
				autoScaleHeld[h].origin = -1;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
		{
			AutoScaleHeld *h = autoScaleHeldFind(s->unit[i].origin, k);
			if (h == NULL)
			{
				if (autoScaleHeldCount == AUTO_SCALE_HELD)
					continue;
				h = &autoScaleHeld[autoScaleHeldCount++];
				h->origin = s->unit[i].origin;
				h->tile = k;
			}
			h->b = s->unit[i].tile[k];
		}
}

static JE_shortint autoScaleStretch(int lead, int speed, int base)
{
	if (lead == 0 || base == 0 || abs(lead) > 30 || abs(speed) > 30 || abs(base) > 30)
		return (JE_shortint)lead;
	const int value = lead + (((lead > 0) == (base > 0)) ? speed - base : base - speed);
	return (JE_shortint)((lead > 0) ? clampi(value, 1, 30) : clampi(value, -30, -1));
}

static void autoScaleRatchet(AutoScaleUnit *u, bool up)
{
	AutoScaleUnit held = *u;
	held.count = 0;
	for (;;)
	{
		const AutoScaleHeld *const h =
		    (held.count < AUTO_SCALE_TILES) ? autoScaleHeldFind(u->origin, held.count) : NULL;
		if (h == NULL)
			break;
		held.tile[held.count++] = h->b;
	}
	if (held.count == 0)
		return;

	const int now = autoScaleUnitMass(u), was = autoScaleUnitMass(&held);
	const int burst = autoScaleUnitPower(u), had = autoScaleUnitPower(&held);
	if (!(up ? now < was : now > was) && !(up ? burst < had : burst > had))
		return;

	const int dx = (autoScaleUnitX2(u) - autoScaleUnitX2(&held)) / 2,
	          dy = (autoScaleUnitY2(u) - autoScaleUnitY2(&held)) / 2;
	const AutoScaleBullet lead = u->tile[0];
	const int heldLead = held.tile[0].sy;
	for (int k = 0; k < held.count; ++k)
	{
		AutoScaleBullet *const t = &held.tile[k];
		t->bx = (JE_shortint)clampi(t->bx + dx, -128, 127);
		t->by = (JE_shortint)clampi(t->by + dy, -128, 127);
		t->sx = lead.sx;
		t->sy = autoScaleStretch(lead.sy, t->sy, heldLead);
		t->del = lead.del;
	}
	*u = held;
}

static int autoScaleUnitCost(const AutoScaleUnit *u, const AutoScaleUnit *s, int cx2)
{
	const AutoScaleBullet *const t = &u->tile[0], *const r = &s->tile[0];
	const int ux2 = autoScaleUnitX2(u), sx2 = autoScaleUnitX2(s);
	if (r->sg == 0 || (t->sg >= 60000) != (r->sg >= 60000) ||
	    (abs(ux2 - cx2) > 12 && abs(sx2) <= 4))
		return -1;
	const int vx = (abs(t->sx) <= 100 && abs(r->sx) <= 100) ? abs(t->sx - r->sx)
	             : (t->sx == r->sx) ? 0 : 20;
	const int vy = (abs(t->sy) <= 100 && abs(r->sy) <= 100) ? abs(t->sy - r->sy)
	             : (t->sy == r->sy) ? 0 : 20;
	return abs(ux2 - sx2) + abs(autoScaleUnitY2(u) - autoScaleUnitY2(s)) + 8 * (vx + vy);
}

static bool autoScaleLineTiles(AutoScaleUnit *u, const AutoScaleShape *stock, int cx2)
{
	int mine[AUTO_SCALE_TILES], theirs[AUTO_SCALE_TILES];
	int best = -1, bestCost = AUTO_SCALE_MATCH + 1;
	if (stock == NULL)
		return false;
	for (int i = 0; i < stock->count; ++i)
	{
		const int cost =
		    (stock->unit[i].count == u->count) ? autoScaleUnitCost(u, &stock->unit[i], cx2) : -1;
		if (cost >= 0 && cost < bestCost)
		{
			best = i;
			bestCost = cost;
		}
	}
	if (best < 0)
		return false;

	const AutoScaleUnit *const s = &stock->unit[best];
	autoScaleTileOrder(u, mine);
	autoScaleTileOrder(s, theirs);
	for (int k = 0; k < u->count; ++k)
	{
		AutoScaleBullet *const t = &u->tile[mine[k]];
		const AutoScaleBullet *const r = &s->tile[theirs[k]];
		const JE_word base = autoScaleBase(r->sg);
		t->sg = (t->sg < 60000 && base < 60000) ? (JE_word)(t->sg - t->sg % 1000 + base) : base;
		if (autoScaleChain(t->attack) && autoScaleChain(r->attack))
			t->attack = r->attack;
	}
	return true;
}

static bool autoScaleLineUses(const JE_word *line, JE_byte code)
{
	for (int l = 0; l < CUSTOM_POWER_LEVELS; ++l)
	{
		const int wn = line[l];
		if (wn <= 0 || wn > WEAP_NUM)
			continue;
		for (int i = 0; i < autoScaleSlots(&weapons[wn]); ++i)
			if (weapons[wn].attack[i] == code)
				return true;
	}
	return false;
}

static bool autoScaleSharedPort(int port)
{
	int users = 0;
	for (int o = 1; o <= OPTION_NUM; ++o)
		users += options[o].wport == port;
	return users >= 3;
}

static bool autoScaleStockPort(int port)
{
	return !customWeaponPortIsCustom((JE_word)port) && weaponPort[port].name[0] != '\0' &&
	       SDL_strcasecmp(weaponPort[port].name, "Test") != 0 && !autoScaleSharedPort(port);
}

static JE_byte autoScaleChainStep(JE_byte code, int step)
{
	JE_byte family[AUTO_SCALE_LADDER];
	int count = 0;
	if (step == 0 || !autoScaleChain(code))
		return code;
	for (int p = 1; p <= PORT_NUM; ++p)
	{
		if (!autoScaleStockPort(p))
			continue;
		for (int m = 0; m < clampi(weaponPort[p].opnum, 1, CUSTOM_WEAPON_MODES); ++m)
		{
			if (!autoScaleLineUses(weaponPort[p].op[m], code))
				continue;
			for (int l = 0; l < CUSTOM_POWER_LEVELS; ++l)
			{
				const int wn = weaponPort[p].op[m][l];
				for (int i = 0; wn > 0 && wn <= WEAP_NUM && i < autoScaleSlots(&weapons[wn]); ++i)
				{
					const JE_byte c = weapons[wn].attack[i];
					bool known = !autoScaleChain(c);
					for (int k = 0; k < count && !known; ++k)
						known = family[k] == c;
					if (!known && count < AUTO_SCALE_LADDER)
						family[count++] = c;
				}
			}
		}
	}

	int rank = 0;
	for (int i = 0; i < count; ++i)
		rank += autoScaleChainDamage(family[i], 2) < autoScaleChainDamage(code, 2);
	int pick = -1, pickRank = 0;
	for (int i = 0; i < count; ++i)
	{
		int r = 0;
		for (int k = 0; k < count; ++k)
			r += autoScaleChainDamage(family[k], 2) < autoScaleChainDamage(family[i], 2);
		const int want = clampi(rank + step, 0, count - 1);
		if (pick < 0 || abs(r - want) < abs(pickRank - want))
		{
			pick = i;
			pickRank = r;
		}
	}
	return (pick >= 0) ? family[pick] : code;
}

static void autoScaleSwapCore(AutoScaleUnit *u, const AutoScaleUnit *s)
{
	const int dx = (autoScaleUnitX2(u) - autoScaleUnitX2(s)) / 2,
	          dy = (autoScaleUnitY2(u) - autoScaleUnitY2(s)) / 2;
	const AutoScaleBullet lead = u->tile[0];
	const int stockLead = s->tile[0].sy;
	u->count = s->count;
	for (int k = 0; k < s->count; ++k)
	{
		AutoScaleBullet *const t = &u->tile[k];
		*t = s->tile[k];
		t->bx = (JE_shortint)clampi(s->tile[k].bx + dx, -128, 127);
		t->by = (JE_shortint)clampi(s->tile[k].by + dy, -128, 127);
		t->sx = lead.sx;
		t->sy = autoScaleStretch(lead.sy, t->sy, stockLead);
		t->del = lead.del;
		t->sg = autoScaleBase(s->tile[k].sg);
		if (lead.sg < 60000 && t->sg < 60000)
			t->sg = (JE_word)(t->sg + lead.sg - lead.sg % 1000);
		t->attack = (autoScaleChain(lead.attack) && autoScaleChain(s->tile[k].attack))
		          ? s->tile[k].attack : lead.attack;
	}
}

static void autoScaleRestyle(AutoScaleShape *out, const AutoScaleShape *stock, int step,
                             bool animated, bool up)
{
	int order[AUTO_SCALE_TILES], twinOrder[AUTO_SCALE_TILES];
	for (int i = 0; i < out->count; ++i)
	{
		AutoScaleUnit *const u = &out->unit[i];
		const bool paired = u->kind == AUTO_SCALE_RING || u->kind == AUTO_SCALE_FLANK;
		const int mate = paired ? autoScaleMate(out, i) : -1;
		if (u->kind == AUTO_SCALE_FLANK && u->side > 0 && mate >= 0)
		{
			for (int k = 0; k < u->count && k < out->unit[mate].count; ++k)
				u->tile[k].sg = out->unit[mate].tile[k].sg;
			continue;
		}

		const int core = (stock != NULL && out->frames == 1 && u->kind == AUTO_SCALE_CORE &&
		                  u->grown == 0 && !autoScaleLattice(out))
		               ? autoScaleCoreUnit(stock, 0) : -1;
		if (core >= 0 && stock->unit[core].count != u->count)
		{
			autoScaleSwapCore(u, &stock->unit[core]);
			autoScaleRatchet(u, up);
			continue;
		}
		if (autoScaleLineTiles(u, stock, out->cx2))
		{
			autoScaleRatchet(u, up);
			continue;
		}

		if (u->kind == AUTO_SCALE_RING && u->side > 0 && mate >= 0)
		{
			AutoScaleUnit r;
			autoScaleUnitMirror(&out->unit[mate], out->cx2, out->csx, &r);
			autoScaleTileOrder(&r, order);
			autoScaleTileOrder(u, twinOrder);
			for (int k = 0; k < r.count && k < u->count; ++k)
				u->tile[twinOrder[k]].sg = r.tile[order[k]].sg;
			continue;
		}

		const bool small = u->grown > 0 && (u->kind != AUTO_SCALE_RING || u->ring == u->grown);
		const int unitStep = small ? step - MIN(u->grown, 2) : step;
		if (u->count > 1 && !animated)
			autoScaleColumnStep(u, unitStep);
		else if (!animated && u->tile[0].sg != 0 && u->tile[0].sg < 60000)
			u->tile[0].sg = (JE_word)(u->tile[0].sg - u->tile[0].sg % 1000 +
			                          autoScaleFamilyStep(autoScaleBase(u->tile[0].sg), unitStep));
		for (int k = 0; k < u->count; ++k)
			u->tile[k].attack = autoScaleChainStep(u->tile[k].attack, unitStep);
		autoScaleRatchet(u, up);
	}
}

static double autoScaleShapeWeight(const AutoScaleShape *s)
{
	double sum = 0.0;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
			sum += autoScaleTileDamage(s->unit[i].tile[k].attack) * s->unit[i].weight;
	return sum;
}

static int autoScaleShapeChain(const AutoScaleShape *s)
{
	int sum = 0;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
			sum += autoScaleChainDamage(s->unit[i].tile[k].attack, 2);
	return sum;
}

static void autoScaleSettle(AutoScaleShape *s, double total)
{
	const double have = autoScaleShapeWeight(s);
	if (have <= 0.0)
		return;
	const double scale = MAX(total - autoScaleShapeChain(s), 0.0) / have;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
		{
			AutoScaleBullet *const t = &s->unit[i].tile[k];
			const int dmg = autoScaleTileDamage(t->attack);
			if (dmg > 0)
				t->attack =
				    autoScaleWithDamage(t->attack, autoScaleRound(dmg * s->unit[i].weight * scale));
		}
}

static double autoScaleShapeDps(const AutoScaleShape *s, int period)
{
	int dmg = autoScaleShapeChain(s);
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
			dmg += autoScaleTileDamage(s->unit[i].tile[k].attack);
	return (double)dmg / MAX(s->frames, 1) * 35.0 / period;
}

static bool autoScaleNudgeTile(AutoScaleBullet *t, int dir)
{
	const int dmg = autoScaleTileDamage(t->attack);
	if (dmg == 0 || autoScaleWithDamage(t->attack, dmg + dir) == t->attack)
		return false;
	t->attack = autoScaleWithDamage(t->attack, dmg + dir);
	return true;
}

static bool autoScaleNudge(AutoScaleShape *s, int dir)
{
	AutoScaleBullet *pick = NULL;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count && s->unit[i].kind == AUTO_SCALE_CORE; ++k)
		{
			AutoScaleBullet *const t = &s->unit[i].tile[k];
			const int dmg = autoScaleTileDamage(t->attack);
			if (dmg == 0 || autoScaleWithDamage(t->attack, dmg + dir) == t->attack)
				continue;
			if (pick == NULL || ((dir < 0) ? dmg > autoScaleTileDamage(pick->attack)
			                               : dmg < autoScaleTileDamage(pick->attack)))
				pick = t;
		}
	if (pick != NULL)
		return autoScaleNudgeTile(pick, dir);

	for (int i = 0; i < s->count; ++i)
	{
		AutoScaleUnit *const u = &s->unit[i];
		if (u->kind == AUTO_SCALE_CORE || u->side > 0)
			continue;
		const int mate = (u->kind == AUTO_SCALE_RING) ? autoScaleMate(s, i) : -1;
		for (int k = 0; k < u->count; ++k)
			if (autoScaleNudgeTile(&u->tile[k], dir))
			{
				if (mate >= 0 && k < s->unit[mate].count)
					autoScaleNudgeTile(&s->unit[mate].tile[k], dir);
				return true;
			}
	}
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
			if (autoScaleNudgeTile(&s->unit[i].tile[k], dir))
				return true;
	return false;
}

static void autoScaleMonotonic(AutoScaleShape *s, int *period, int fastest, int slowest,
                               double limit, bool above)
{
	for (int guard = 0; guard < 400; ++guard)
	{
		const double dps = autoScaleShapeDps(s, *period);
		if (above ? dps >= limit : dps <= limit)
			return;
		if (autoScaleNudge(s, above ? 1 : -1))
			continue;
		if (above ? *period <= fastest : *period >= slowest)
			return;
		*period += above ? -1 : 1;
	}
}

static bool autoScaleClear(AutoScaleShape *s, int period, double limit, bool above)
{
	for (int guard = 0; guard < 64; ++guard)
	{
		const double dps = autoScaleShapeDps(s, period);
		if (above ? dps >= limit : dps <= limit)
			return true;
		if (!autoScaleNudge(s, above ? 1 : -1))
			return false;
	}
	return false;
}

static void autoScaleWrite(const AutoScaleShape *s, const JE_WeaponType *anchor, int period,
                           JE_WeaponType *w)
{
	static AutoScaleBullet tiles[CUSTOM_BULLETS_MAX];
	int n = 0;
	for (int f = 0; f < s->frames; ++f)
	{
		const int first = n;
		for (int i = 0; i < s->count; ++i)
			for (int k = 0; k < s->unit[i].count && s->unit[i].frame == f && n < CUSTOM_BULLETS_MAX;
			     ++k)
				tiles[n++] = s->unit[i].tile[k];
		for (int i = first + 1; i < n; ++i)
		{
			const AutoScaleBullet t = tiles[i];
			int j = i;
			for (; j > first &&
			       (tiles[j - 1].bx > t.bx || (tiles[j - 1].bx == t.bx && tiles[j - 1].by > t.by));
			     --j)
				tiles[j] = tiles[j - 1];
			tiles[j] = t;
		}
	}

	const int clearTo = MAX(clampi(anchor->multi, 1, CUSTOM_BULLETS_MAX),
	                        clampi(anchor->max, 1, CUSTOM_BULLETS_MAX));
	*w = *anchor;
	w->shotrepeat = (JE_byte)(period - 1);
	for (int i = 0; i < n; ++i)
		autoScalePut(w, i, &tiles[i]);
	for (int i = n; i < clearTo; ++i)
	{
		const AutoScaleBullet empty = { 0, 0, 0, 0, 0, 0, 0 };
		autoScalePut(w, i, &empty);
	}
	w->multi = (JE_byte)(n / MAX(s->frames, 1));
	w->max = (JE_byte)n;
}

static int autoScaleLevelStep(int p, int a)
{
	const int step = (int)(abs(p - a) * 0.4 + 0.5);
	return clampi((p < a) ? -step : step, -3, 3);
}

static void autoScaleRigid(const JE_WeaponType *anchor, int p, int a, double scale, int period,
                           JE_WeaponType *w)
{
	*w = *anchor;
	w->shotrepeat = (JE_byte)(period - 1);
	const int slots = MAX(clampi(anchor->multi, 1, CUSTOM_BULLETS_MAX),
	                      clampi(anchor->max, 1, CUSTOM_BULLETS_MAX));
	const int step = (anchor->weapani > 0) ? 0 : autoScaleLevelStep(p, a);
	for (int i = 0; i < slots; ++i)
	{
		const int dmg = autoScaleTileDamage(w->attack[i]);
		if (dmg > 0)
			w->attack[i] = autoScaleWithDamage(w->attack[i], autoScaleRound(dmg * scale));
		if (w->sg[i] != 0 && w->sg[i] < 60000)
		{
			const JE_word spark = (JE_word)(w->sg[i] - w->sg[i] % 1000);
			w->sg[i] = (JE_word)(spark + autoScaleFamilyStep((JE_word)(w->sg[i] % 1000), step));
		}
	}
}

static int autoScaleLineWeapon(int line, int level)
{
	if (line > 0)
	{
		const int port = line / CUSTOM_WEAPON_MODES, mode = line % CUSTOM_WEAPON_MODES;
		for (int d = 0; d < CUSTOM_POWER_LEVELS; ++d)
			for (int k = -1; k <= 1; k += 2)
			{
				const int l = level + k * d;
				if (l < 0 || l >= CUSTOM_POWER_LEVELS)
					continue;
				const int wn = weaponPort[port].op[mode][l];
				if (wn > 0 && wn <= WEAP_NUM && weapons[wn].sg[0] != 0)
					return wn;
			}
		return 0;
	}
	const int option = -line;
	const int stages = clampi(options[option].pwr + 1, 1, CUSTOM_POWER_LEVELS);
	const int wn = options[option].wpnum + clampi(level, 0, stages - 1);
	return (wn > 0 && wn <= WEAP_NUM) ? wn : 0;
}

static int autoScaleLikeness(const JE_WeaponType *a, const JE_WeaponType *w)
{
	const int na = autoScaleSlots(a), nw = autoScaleSlots(w);
	bool exact = a->multi == w->multi && a->max == w->max;
	int shared = 0;
	for (int i = 0; i < na; ++i)
	{
		bool found = false;
		for (int j = 0; j < nw && !found; ++j)
			found = autoScaleBase(a->sg[i]) == autoScaleBase(w->sg[j]);
		shared += found;
		exact = exact && i < nw && a->bx[i] == w->bx[i] && a->by[i] == w->by[i] &&
		        a->sx[i] == w->sx[i] && a->sy[i] == w->sy[i] &&
		        autoScaleBase(a->sg[i]) == autoScaleBase(w->sg[i]);
	}
	return shared * 100 / na + (exact ? 500 : 0);
}

static void autoScaleConsider(const JE_WeaponType *anchor, int anchorLevel, int line, int level,
                              int *best, int *bestLevel, int *bestScore)
{
	const int wn = autoScaleLineWeapon(line, level);
	if (wn <= 0)
		return;
	const int score = autoScaleLikeness(anchor, &weapons[wn]) * 16 - abs(level - anchorLevel);
	if (score > *bestScore)
	{
		*best = line;
		*bestLevel = level;
		*bestScore = score;
	}
}

static int autoScaleLineage(const JE_WeaponType *anchor, int anchorLevel, int *lineLevel)
{
	int best = 0, bestScore = 50 * 16 - CUSTOM_POWER_LEVELS;
	for (int p = 1; p <= PORT_NUM; ++p)
	{
		if (!autoScaleStockPort(p))
			continue;
		for (int m = 0; m < clampi(weaponPort[p].opnum, 1, CUSTOM_WEAPON_MODES); ++m)
			for (int l = 0; l < CUSTOM_POWER_LEVELS; ++l)
				if (weaponPort[p].op[m][l] > 0)
					autoScaleConsider(anchor, anchorLevel, p * CUSTOM_WEAPON_MODES + m, l, &best,
					                  lineLevel, &bestScore);
	}
	for (int o = 1; o <= OPTION_NUM; ++o)
		if (!customSidekickSlotIsCustom(o) && options[o].pwr > 0)
			for (int l = 0; l <= MIN(options[o].pwr, CUSTOM_POWER_LEVELS - 1); ++l)
				autoScaleConsider(anchor, anchorLevel, -o, l, &best, lineLevel, &bestScore);
	return best;
}

static int autoScaleVolleyArea(const AutoScaleShape *s)
{
	int area = 0;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count && s->unit[i].frame == 0; ++k)
		{
			const JE_word sg = s->unit[i].tile[k].sg;
			Sprite2_array sheet;
			unsigned int index;
			JE_word offset;
			if (autoScaleSpriteRef(sg, &sheet, &index, &offset))
				area += autoScaleSpriteArea(sheet, index);
			else if (sg > 60000 && (unsigned int)(sg - 60001) < COUNTOF(sprite_table[0].sprite))
				area += get_sprite_width(OPTION_SHAPES, sg - 60001) *
				        get_sprite_height(OPTION_SHAPES, sg - 60001);
		}
	return area;
}

static int autoScaleRingCap(int period)
{
	return (period <= 2) ? 1 : (period <= 4) ? 2 : 3;
}

static bool autoScaleWeave(AutoScaleShape *s)
{
	bool sided = false;
	for (int i = 0; i < s->count; ++i)
		sided |= (s->unit[i].kind == AUTO_SCALE_RING || s->unit[i].kind == AUTO_SCALE_FLANK) &&
		         s->unit[i].side != 0;
	if (s->frames != 1 || !sided)
		return true;

	const int count = s->count;
	for (int i = 0; i < count; ++i)
	{
		AutoScaleUnit *const u = &s->unit[i];
		if ((u->kind == AUTO_SCALE_RING || u->kind == AUTO_SCALE_FLANK) && u->side != 0)
		{
			u->frame = (u->side > 0) ? 1 : 0;
			continue;
		}
		AutoScaleUnit twin = *u;
		twin.frame = 1;
		if (!autoScaleAddUnit(s, &twin))
			return false;
	}
	s->frames = 2;
	return autoScaleFramesEven(s) && 2 * autoScaleFrameTiles(s, 0) <= CUSTOM_BULLETS_MAX;
}

static bool autoScaleForm(const AutoScaleShape *src, int p, int a, int plan,
                          const AutoScaleShape *stock, bool animated, AutoScaleShape *out)
{
	const bool ok =
	    (p < a) ? autoScaleShrink(src, (double)plan / a, out) : autoScaleGrow(src, plan, out);
	if (ok)
		autoScaleRestyle(out, stock, autoScaleLevelStep(p, a), animated, p > a);
	return ok && (autoScaleStyle != CUSTOM_SPREAD_WEAVE || autoScaleWeave(out));
}

static int autoScaleShapeTiles(const AutoScaleShape *s)
{
	int tiles = 0;
	for (int i = 0; i < s->count; ++i)
		tiles += s->unit[i].count;
	return tiles;
}

static void autoScaleFlair(const AutoScaleShape *s, const JE_WeaponType *anchor, int p,
                           JE_WeaponType *w)
{
	bool grown = false;
	for (int i = 0; i < s->count; ++i)
		grown |= s->unit[i].grown > 0;
	if (autoScaleStyle == CUSTOM_SPREAD_LOB && anchor->acceleration == 0 && grown)
		w->acceleration = (JE_shortint)s->fwd;
	if (autoScaleStyle == CUSTOM_SPREAD_SPIRAL && anchor->circlesize == 0)
	{
		w->circlesize = (JE_byte)(2 + p / 3);
		const int dx = autoScaleSway2(w->circlesize) / 2;
		for (int i = 0; i < autoScaleSlots(w); ++i)
			w->bx[i] = (JE_shortint)clampi(w->bx[i] - dx, -128, 127);
	}
}

static bool autoScaleSameLook(const JE_WeaponType *x, const JE_WeaponType *y)
{
	const int n = autoScaleSlots(x);
	if (x->multi != y->multi || x->max != y->max)
		return false;
	for (int i = 0; i < n; ++i)
	{
		bool found = false;
		for (int j = 0; j < n && !found; ++j)
			found = autoScaleBase(x->sg[i]) == autoScaleBase(y->sg[j]) && x->bx[i] == y->bx[j] &&
			        x->by[i] == y->by[j] && x->sx[i] == y->sx[j] && x->sy[i] == y->sy[j];
		if (!found)
			return false;
	}
	return true;
}

static bool autoScaleLineFits(int line, int lineLevel, int a)
{
	const int top = (line > 0) ? CUSTOM_POWER_LEVELS - 1
	              : clampi(options[-line].pwr, 0, CUSTOM_POWER_LEVELS - 1);
	int looks = 0, clamped = 0;
	for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
	{
		const int at = lineLevel + p - a;
		clamped += at > top;
		const int wn = autoScaleLineWeapon(line, clampi(at, 0, top));
		bool seen = wn <= 0;
		for (int k = 0; k < p && !seen; ++k)
		{
			const int other = autoScaleLineWeapon(line, clampi(lineLevel + k - a, 0, top));
			seen = other > 0 && autoScaleSameLook(&weapons[other], &weapons[wn]);
		}
		looks += !seen;
	}
	return looks >= 5 && clamped <= 1;
}

static int autoScaleLooks(const JE_WeaponType *levels)
{
	int looks = 0;
	for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
	{
		bool seen = false;
		for (int k = 0; k < p && !seen; ++k)
			seen = autoScaleSameLook(&levels[k], &levels[p]);
		looks += !seen;
	}
	return looks;
}

static bool autoScaleSameSlot(const JE_WeaponType *w, int i, int j, bool mirror)
{
	const JE_word sg = mirror ? autoScaleMirrorSprite(w->sg[j]) : w->sg[j];
	const int sx = (mirror && abs(w->sx[j]) <= 100) ? -w->sx[j] : w->sx[j];
	const int bx = mirror ? -w->bx[j] - autoScaleOverhang(w->sg[j]) : w->bx[j];
	return w->bx[i] == bx && w->by[i] == w->by[j] && w->sx[i] == sx && w->sy[i] == w->sy[j] &&
	       w->sg[i] == sg && w->attack[i] == w->attack[j] && w->del[i] == w->del[j];
}

static void autoScaleUnweave(JE_WeaponType *w)
{
	const int multi = clampi(w->multi, 1, CUSTOM_BULLETS_MAX);
	if (w->max != 2 * multi)
		return;
	bool echo[CUSTOM_BULLETS_MAX] = { false };
	for (int i = multi; i < 2 * multi; ++i)
	{
		bool mirrored = false;
		for (int j = 0; j < multi && !mirrored && !echo[i]; ++j)
		{
			echo[i] = autoScaleSameSlot(w, i, j, false);
			mirrored = autoScaleSameSlot(w, i, j, true);
		}
		if (!mirrored && !echo[i])
			return;
	}
	int n = multi;
	for (int i = multi; i < 2 * multi; ++i)
		if (!echo[i])
		{
			w->sg[n] = w->sg[i];
			w->attack[n] = w->attack[i];
			w->del[n] = w->del[i];
			w->sx[n] = w->sx[i];
			w->sy[n] = w->sy[i];
			w->bx[n] = w->bx[i];
			w->by[n] = w->by[i];
			++n;
		}
	w->multi = w->max = (JE_byte)n;
}

static int autoScaleUsableStyle(int style, bool steer)
{
	if (!steer && (style == CUSTOM_SPREAD_FAN || style == CUSTOM_SPREAD_BURST ||
	               style == CUSTOM_SPREAD_LOB || style == CUSTOM_SPREAD_SPIRAL))
		return CUSTOM_SPREAD_ARROW;
	if (!steer && (style == CUSTOM_SPREAD_CONVERGE || style == CUSTOM_SPREAD_NOVA))
		return CUSTOM_SPREAD_WALL;
	return style;
}

static int autoScaleRolled = CUSTOM_SPREAD_AUTO;

static int autoScalePickStyle(const JE_WeaponType *anchor, bool chosen)
{
	bool steer = false;
	for (int i = 0; i < autoScaleSlots(anchor); ++i)
	{
		const AutoScaleBullet b = autoScaleGet(anchor, i);
		steer |= b.sg != 0 && autoScaleAimable(&b);
	}
	if (chosen)
	{
		autoScaleRolled = autoScaleUsableStyle(customWeaponSpread, steer);
		return autoScaleRolled;
	}

	int pool[CUSTOM_SPREAD_COUNT], n = 0;
	for (int style = CUSTOM_SPREAD_ARROW; style < CUSTOM_SPREAD_COUNT; ++style)
	{
		const int usable = autoScaleUsableStyle(style, steer);
		bool seen = usable == autoScaleRolled;
		for (int k = 0; k < n && !seen; ++k)
			seen = pool[k] == usable;
		if (!seen)
			pool[n++] = usable;
	}
	autoScaleRolled = pool[((Uint32)customRand(65536) + SDL_GetTicks()) % (Uint32)n];
	return autoScaleRolled;
}

static const int autoScaleCurve[CUSTOM_POWER_LEVELS] = { 100, 140, 180, 220, 260, 300, 350, 400, 460, 530, 600 };

static void autoScaleRawScale(JE_WeaponType *w, const JE_WeaponType *stock, double target)
{
	const int slots = autoScaleSlots(w), multi = clampi(w->multi, 1, CUSTOM_BULLETS_MAX);
	int scalable = 0, fixed = 0;
	for (int i = 0; i < slots; ++i)
	{
		scalable += autoScaleTileDamage(stock->attack[i]);
		fixed += autoScaleChainDamage(stock->attack[i], 2);
	}
	if (scalable == 0)
		return;
	const double want = target * (w->shotrepeat + 1) * slots / (35.0 * multi) - fixed;
	const double scale = MAX(want, 0.0) / scalable;
	for (int i = 0; i < slots; ++i)
	{
		const int dmg = autoScaleTileDamage(stock->attack[i]);
		if (dmg > 0)
			w->attack[i] = autoScaleWithDamage(stock->attack[i], autoScaleRound(dmg * scale));
	}
}

static bool autoScaleRawNudge(JE_WeaponType *w, int dir)
{
	int pick = -1;
	for (int i = 0; i < autoScaleSlots(w); ++i)
	{
		const int dmg = autoScaleTileDamage(w->attack[i]);
		if (dmg == 0 || autoScaleWithDamage(w->attack[i], dmg + dir) == w->attack[i])
			continue;
		if (pick < 0 || ((dir > 0) ? dmg < autoScaleTileDamage(w->attack[pick])
		                           : dmg > autoScaleTileDamage(w->attack[pick])))
			pick = i;
	}
	if (pick < 0)
		return false;
	w->attack[pick] =
	    autoScaleWithDamage(w->attack[pick], autoScaleTileDamage(w->attack[pick]) + dir);
	return true;
}

static double autoScaleRawMiss(double dps, double target)
{
	return (dps > 0.0 && target > 0.0) ? fabs(log(dps / target)) : 99.0;
}

static bool autoScaleRawBreaks(double dps, double limit, bool up)
{
	return limit > 0.0 && (up ? dps < limit - 1e-6 : dps > limit + 1e-6);
}

static double autoScaleRawTry(JE_WeaponType *w, const JE_WeaponType *stock, int period,
                              double target, double limit, bool up)
{
	w->shotrepeat = (JE_byte)(period - 1);
	autoScaleRawScale(w, stock, target);
	double dps = autoScaleWeaponDps(w);
	for (int guard = 0; guard < 64; ++guard)
	{
		JE_WeaponType next = *w;
		if (!autoScaleRawNudge(&next, (dps < target) ? 1 : -1))
			break;
		const double moved = autoScaleWeaponDps(&next);
		if (autoScaleRawMiss(moved, target) >= autoScaleRawMiss(dps, target))
			break;
		*w = next;
		dps = moved;
	}
	for (int guard = 0; guard < 64 && autoScaleRawBreaks(dps, limit, up); ++guard)
	{
		if (!autoScaleRawNudge(w, up ? 1 : -1))
			break;
		dps = autoScaleWeaponDps(w);
	}
	return dps;
}

static void autoScaleRawFit(JE_WeaponType *w, const JE_WeaponType *stock, int base, double target,
                            double limit, bool up)
{
	int scalable = 0;
	for (int i = 0; i < autoScaleSlots(stock); ++i)
		scalable += autoScaleTileDamage(stock->attack[i]);
	const JE_WeaponType from = *w;
	const double dps = autoScaleRawTry(w, stock, base, target, limit, up);
	if (base <= 1 || (!autoScaleRawBreaks(dps, limit, up) &&
	                  (scalable == 0 || autoScaleRawMiss(dps, target) <= 0.2)))
		return;
	double bestScore = 5.0 * autoScaleRawBreaks(dps, limit, up) +
	                   ((scalable > 0) ? autoScaleRawMiss(dps, target) : 0.0);
	for (int period = MAX(autoScaleRound(base * 0.5), 1); period <= MIN(base * 2, 256); ++period)
	{
		JE_WeaponType t = from;
		const double moved = autoScaleRawTry(&t, stock, period, target, limit, up);
		const double score = 5.0 * autoScaleRawBreaks(moved, limit, up) +
		                     ((scalable > 0) ? autoScaleRawMiss(moved, target) : 0.0) +
		                     0.5 * fabs(log((double)period / base));
		if (score < bestScore)
		{
			*w = t;
			bestScore = score;
		}
	}
}

static double autoScaleLineHeft(int line, int level)
{
	double heft = 0.0;
	for (int q = 0; q <= level; ++q)
	{
		const int wn = autoScaleLineWeapon(line, q);
		if (wn > 0)
			heft = MAX(heft, autoScaleWeaponDps(&weapons[wn]) * pow(1.08, level - q));
	}
	return heft;
}

static double autoScaleLineRatio(int line, int lineLevel, int a, int p)
{
	const int at = lineLevel + p - a, end = clampi(at, 0, CUSTOM_POWER_LEVELS - 1);
	const double from = autoScaleWeaponDps(&weapons[autoScaleLineWeapon(line, lineLevel)]);
	const double reach = autoScaleLineHeft(line, end);
	if (from <= 0.0 || reach <= 0.0)
		return (double)autoScaleCurve[p] / autoScaleCurve[a];
	return reach / from * autoScaleCurve[p] / autoScaleCurve[p - (at - end)];
}

static void autoScaleFollowLine(const JE_WeaponType *anchor, int m, int a, int line, int lineLevel)
{
	const JE_WeaponType *const x = &weapons[autoScaleLineWeapon(line, lineLevel)];
	const double pace = (double)(anchor->shotrepeat + 1) / (x->shotrepeat + 1);
	const double anchorDps = autoScaleWeaponDps(anchor);
	bool beam = false;
	for (int i = 0; i < autoScaleSlots(anchor); ++i)
		beam |= anchor->shotrepeat == 0 && x->shotrepeat == 0 && abs(anchor->sx[i]) > 100 &&
		        anchor->sg[i] != 0;
	for (int dir = -1; dir <= 1; dir += 2)
	{
		double limit = anchorDps;
		for (int p = a + dir; p >= 0 && p < CUSTOM_POWER_LEVELS; p += dir)
		{
			const int wn =
			    autoScaleLineWeapon(line, clampi(lineLevel + p - a, 0, CUSTOM_POWER_LEVELS - 1));
			if (wn <= 0)
				continue;
			JE_WeaponType w = weapons[wn];
			const int base = beam ? 1 : clampi(autoScaleRound((w.shotrepeat + 1) * pace), 1, 256);
			w.shotrepeat = (JE_byte)(base - 1);
			if (anchor->sound != x->sound)
				w.sound = anchor->sound;
			if (anchor->aim != x->aim)
				w.aim = anchor->aim;
			if (anchor->acceleration != x->acceleration)
				w.acceleration = anchor->acceleration;
			if (anchor->accelerationx != x->accelerationx)
				w.accelerationx = anchor->accelerationx;
			if (anchor->circlesize != x->circlesize)
				w.circlesize = anchor->circlesize;
			if (anchor->trail != x->trail)
				w.trail = anchor->trail;
			if (anchor->shipblastfilter != x->shipblastfilter)
				w.shipblastfilter = anchor->shipblastfilter;
			const double target = anchorDps * autoScaleLineRatio(line, lineLevel, a, p);
			if (anchorDps > 0.0)
				autoScaleRawFit(&w, &weapons[wn], base, target, limit, dir > 0);
			customWeaponRaw[m][p] = w;
			limit = autoScaleWeaponDps(&w);
		}
	}
}

void customWeaponAutoScaleLevels(void)
{
	const int m = clampi(customWeaponEditMode,  0, CUSTOM_WEAPON_MODES - 1);
	const int a = clampi(customWeaponEditLevel, 0, CUSTOM_POWER_LEVELS - 1);
	const JE_WeaponType anchor = customWeaponRaw[m][a];
	const int basePeriod = anchor.shotrepeat + 1;
	JE_WeaponType source = anchor;
	autoScaleUnweave(&source);
	const bool shaped = autoScaleDecompose(&source, &autoScaleAnchor);
	const bool animated = anchor.weapani > 0;

	int lineLevel = 0;
	const int line = shaped ? autoScaleLineage(&anchor, a, &lineLevel) : 0;
	static JE_WeaponType followed[CUSTOM_POWER_LEVELS];
	const int lineWn = (line != 0) ? autoScaleLineWeapon(line, lineLevel) : 0;
	const bool stockLine = lineWn > 0 && autoScaleLikeness(&anchor, &weapons[lineWn]) >= 500;
	const bool stockFits = stockLine && autoScaleLineFits(line, lineLevel, a);
	const bool chosen =
	    customWeaponSpread >= (stockLine ? CUSTOM_SPREAD_STOCK : CUSTOM_SPREAD_ARROW) &&
	    customWeaponSpread < CUSTOM_SPREAD_COUNT;
	autoScaleStyle = autoScalePickStyle(&anchor, chosen);
	const bool follow = autoScaleStyle == CUSTOM_SPREAD_STOCK;
	if (follow)
	{
		autoScaleFollowLine(&anchor, m, a, line, lineLevel);
		customWeaponLastSpread = CUSTOM_SPREAD_STOCK;
		if (stockFits)
			return;
		memcpy(followed, customWeaponRaw[m], sizeof(followed));
		autoScaleStyle = CUSTOM_SPREAD_ARROW;
	}
	customWeaponLastSpread = autoScaleStyle;
	const bool lattice = shaped && autoScaleLattice(&autoScaleAnchor);
	const double anchorDps = autoScaleWeaponDps(&anchor);
	const bool damaging = anchorDps > 0.0;
	const int fastest = (basePeriod <= 2) ? 1 : MAX(autoScaleRound(basePeriod * 0.4), 2);
	bool beam = false;
	for (int i = 0; i < autoScaleSlots(&anchor); ++i)
		beam |= basePeriod <= 1 && abs(anchor.sx[i]) > 100 && anchor.sg[i] != 0;

	for (int dir = -1; dir <= 1; dir += 2)
	{
		double limit = anchorDps;
		int trend = basePeriod;
		int volley = clampi(anchor.multi, 1, CUSTOM_BULLETS_MAX);
		if (shaped)
			volley = autoScaleShapeTiles(&autoScaleAnchor);
		autoScaleHeldCount = 0;
		if (shaped)
			autoScaleHold(&autoScaleAnchor);
		int heft = shaped ? autoScaleVolleyArea(&autoScaleAnchor) : 0;
		for (int p = a + dir; p >= 0 && p < CUSTOM_POWER_LEVELS; p += dir)
		{
			const double ratio = (double)autoScaleCurve[p] / autoScaleCurve[a];
			const double target = anchorDps * ratio;
			const int planned =
			    beam ? 1 : clampi(autoScaleRound(basePeriod / pow(ratio, 0.25)), fastest, 256);
			if (!shaped)
			{
				const int period = (dir < 0) ? MAX(planned, trend) : MIN(planned, trend);
				autoScaleRigid(&anchor, p, a, ratio * period / basePeriod, period,
				               &customWeaponRaw[m][p]);
				trend = period;
				continue;
			}

			const int lineTo = (line != 0) ? autoScaleLineWeapon(line, lineLevel + p - a) : 0;
			const JE_WeaponType *const stock =
			    (lineTo > 0 && (!animated || weapons[lineTo].weapani == anchor.weapani))
			        ? &weapons[lineTo]
			        : NULL;
			const AutoScaleShape *const stockShape =
			    (stock != NULL && autoScaleDecompose(stock, &autoScaleStock)) ? &autoScaleStock
			                                                                  : NULL;

			const int growth = lattice ? 2 : 1;
			const int highestPlan = (p < a) ? p : growth * autoScaleRingCap(basePeriod);
			const double stride =
			    MIN(0.25 * growth, (double)highestPlan / MAX(CUSTOM_POWER_LEVELS - 1 - a, 1));
			const int plannedPlan = (p < a) ? p : MIN((int)((p - a) * stride + 0.5), highestPlan);
			int bestPlan = -1, bestPush = 0;
			int bestPeriod = (dir < 0) ? MAX(planned, trend) : MIN(planned, trend);
			double bestScore = 0.0;
			for (int plan = highestPlan; plan >= 0; --plan)
			{
				if (!autoScaleForm(&autoScaleAnchor, p, a, plan, stockShape, animated,
				                   &autoScaleLevel))
					continue;
				const int tiles = autoScaleShapeTiles(&autoScaleLevel);
				if ((dir > 0) ? tiles < volley : tiles > volley)
					continue;
				const int area = autoScaleVolleyArea(&autoScaleLevel);
				const bool heavier = tiles > volley || area * 5 >= heft * 6;
				const bool lighter = tiles < volley || area * 6 <= heft * 5;
				const int lowPeriod = beam        ? 1
				                      : (dir < 0) ? MAX(lighter ? trend / 2 : trend - 1,
				                                        MAX(fastest, MIN(basePeriod, 2)))
				                                  : clampi(MIN(trend, planned) - 4, fastest, 256);
				const int highPeriod =
				    beam        ? 1
				    : (dir < 0) ? MIN(MAX(trend, planned) + 4, 256)
				                : MIN(heavier ? MIN(trend * 2, trend + 12) : trend + 1, 256);
				for (int trial = 0; trial < 2 * (highPeriod - lowPeriod + 1); ++trial)
				{
					const int period = lowPeriod + trial / 2, push = trial % 2;
					autoScaleTrial = autoScaleLevel;
					autoScaleSettle(&autoScaleTrial,
					                target * period * autoScaleTrial.frames / 35.0);
					if (damaging && !autoScaleClear(&autoScaleTrial, period, limit, dir > 0))
						continue;
					if (push && (!damaging || !autoScaleNudge(&autoScaleTrial, dir)))
						continue;
					const int against = (dir > 0) ? period - trend : trend - period;
					const double slip = ((dir > 0) ? heavier : lighter) ? 0.03 : 0.1;
					const double dps = autoScaleShapeDps(&autoScaleTrial, period);
					const double miss = damaging ? fabs(log(dps / target)) : 0.0;
					const double stall = (damaging && fabs(dps - limit) < 0.001) ? 0.25 : 0.0;
					const double score = 0.3 * abs(plannedPlan - plan) +
					                     0.02 * abs(period - planned) + slip * MAX(against, 0) +
					                     0.5 * miss + ((miss > 0.7) ? 2.0 + miss : 0.0) + stall;
					if (bestPlan < 0 || score < bestScore)
					{
						bestPlan = plan;
						bestPeriod = period;
						bestPush = push;
						bestScore = score;
					}
				}
			}

			int period = bestPeriod;
			int plan = (bestPlan >= 0) ? bestPlan : plannedPlan;
			while (!autoScaleForm(&autoScaleAnchor, p, a, plan, stockShape, animated,
			                      &autoScaleLevel))
			{
				if (plan == 0)
				{
					autoScaleLevel = autoScaleAnchor;
					break;
				}
				--plan;
			}
			autoScaleSettle(&autoScaleLevel, target * period * autoScaleLevel.frames / 35.0);
			if (damaging && !autoScaleClear(&autoScaleLevel, period, limit, dir > 0))
				autoScaleMonotonic(&autoScaleLevel, &period, fastest, beam ? 1 : 256, limit,
				                   dir > 0);
			if (bestPush)
				autoScaleNudge(&autoScaleLevel, dir);
			autoScaleWrite(&autoScaleLevel, &anchor, period, &customWeaponRaw[m][p]);
			autoScaleFlair(&autoScaleLevel, &anchor, p, &customWeaponRaw[m][p]);
			autoScaleHold(&autoScaleLevel);
			limit = autoScaleShapeDps(&autoScaleLevel, period);
			trend = period;
			volley = autoScaleShapeTiles(&autoScaleLevel);
			heft = autoScaleVolleyArea(&autoScaleLevel);
		}
	}
	if (follow && autoScaleLooks(followed) > autoScaleLooks(customWeaponRaw[m]))
	{
		memcpy(customWeaponRaw[m], followed, sizeof(followed));
		customWeaponLastSpread = CUSTOM_SPREAD_STOCK;
	}
}

// The design (raw weapon) the editor is currently pointed at.
static JE_WeaponType *customEditingRaw(void)
{
	const int m = clampi(customWeaponEditMode,  0, CUSTOM_WEAPON_MODES - 1);
	const int p = clampi(customWeaponEditLevel, 0, CUSTOM_POWER_LEVELS - 1);
	return &customWeaponRaw[m][p];
}

int customWeaponAddBullet(int afterIndex)
{
	JE_WeaponType *w = customEditingRaw();
	const int n = clampi(w->multi, 1, CUSTOM_BULLETS_MAX);
	if (n >= CUSTOM_BULLETS_MAX)
		return -1;   // already at CUSTOM_BULLETS_MAX

	const int from = clampi(afterIndex, 0, n - 1);
	const int at   = from + 1;   // insert the copy directly after its source

	// Open a gap at `at` by shifting the higher segments up one slot.
	for (int i = n; i > at; --i)
	{
		w->sg[i] = w->sg[i - 1];  w->attack[i] = w->attack[i - 1];  w->del[i] = w->del[i - 1];
		w->sx[i] = w->sx[i - 1];  w->sy[i]     = w->sy[i - 1];
		w->bx[i] = w->bx[i - 1];  w->by[i]     = w->by[i - 1];
	}

	// The new segment duplicates its source, nudged sideways so the two don't overlap.
	w->sg[at] = w->sg[from];  w->attack[at] = w->attack[from];  w->del[at] = w->del[from];
	w->sx[at] = w->sx[from];  w->sy[at]     = w->sy[from];
	w->bx[at] = (JE_shortint)clampi(w->bx[from] + 8, -128, 127);
	w->by[at] = w->by[from];

	w->multi = (JE_byte)(n + 1);
	if (w->max < w->multi)   // keep the pattern cycle at least as long as the bullet count
		w->max = w->multi;
	return at;
}

int customWeaponRemoveBullet(int index)
{
	JE_WeaponType *w = customEditingRaw();
	const int n = clampi(w->multi, 1, CUSTOM_BULLETS_MAX);
	if (n <= 1)
		return -1;   // a design must keep at least one bullet

	const int at = clampi(index, 0, n - 1);

	// Close the gap by shifting the higher segments down one slot.
	for (int i = at; i < n - 1; ++i)
	{
		w->sg[i] = w->sg[i + 1];  w->attack[i] = w->attack[i + 1];  w->del[i] = w->del[i + 1];
		w->sx[i] = w->sx[i + 1];  w->sy[i]     = w->sy[i + 1];
		w->bx[i] = w->bx[i + 1];  w->by[i]     = w->by[i + 1];
	}

	// Clear the now-unused top slot so a later Add starts from a clean segment.
	const int last = n - 1;
	w->sg[last] = 0;  w->attack[last] = 0;  w->del[last] = 0;
	w->sx[last] = 0;  w->sy[last] = 0;  w->bx[last] = 0;  w->by[last] = 0;

	w->multi = (JE_byte)(n - 1);
	// Keep `max`; variation patterns may have more stages than active segments.
	return (at >= n - 1) ? at - 1 : at;   // select the segment now occupying this slot
}

int customWeaponAddChargeState(void)
{
	if (customWeaponChargeStages >= CUSTOM_POWER_LEVELS)
		return -1;   // every power level is already a charge state

	++customWeaponChargeStages;
	// Select the new top stage for editing.
	customWeaponEditLevel = customWeaponChargeStages - 1;
	return customWeaponEditLevel;
}

int customWeaponRemoveChargeState(void)
{
	if (customWeaponChargeStages <= 1)
		return -1;   // one shot left = no charging; can't remove further

	--customWeaponChargeStages;
	if (customWeaponEditLevel >= customWeaponChargeStages)
		customWeaponEditLevel = customWeaponChargeStages - 1;   // keep the edit level in range
	return customWeaponEditLevel;
}

void customWeaponRandomize(void)
{
	// Prefix + noun, "Neon Blaster" style. Big pools = thousands of combinations; every
	// word is short enough that any pair fits the 30-char name buffer.
	static const char *const parts1[] = {
		"Neon", "Hyper", "Chaos", "Star", "Vortex", "Plasma", "Ion", "Rift",
		"Quantum", "Nova", "Solar", "Cosmic", "Astral", "Void", "Nebula", "Photon",
		"Gamma", "Omega", "Delta", "Sigma", "Turbo", "Mega", "Ultra", "Cyber",
		"Atomic", "Nuclear", "Fusion", "Pulse", "Volt", "Blaze", "Frost", "Thunder",
		"Shadow", "Crimson", "Azure", "Ember", "Static", "Havoc", "Doom", "Fury",
		"Lunar", "Stellar", "Prism", "Zenith", "Apex", "Titan", "Warp", "Flux",
		"Spectral", "Radiant", "Searing", "Venom", "Dread", "Savage", "Wraith", "Onyx",
		"Cobalt", "Scarlet", "Viper", "Phantom", "Inferno", "Tempest", "Cyclone", "Meteor",
		"Pulsar", "Quasar", "Prime", "Hex", "Neutron", "Tesla", "Sonic", "Blitz",
		"Rogue", "Feral", "Molten", "Frenzy", "Rune", "Aether", "Chrome", "Storm",
	};
	static const char *const parts2[] = {
		"Blaster", "Cannon", "Spray", "Lance", "Storm", "Fang", "Burst", "Ray",
		"Beam", "Blast", "Bomb", "Rifle", "Repeater", "Driver", "Launcher", "Reaper",
		"Striker", "Slayer", "Breaker", "Shredder", "Piercer", "Wave", "Barrage", "Volley",
		"Salvo", "Hammer", "Spike", "Needle", "Dagger", "Blade", "Edge", "Claw",
		"Talon", "Sting", "Jet", "Stream", "Torrent", "Flare", "Coil", "Whip",
		"Scythe", "Saber", "Disruptor", "Annihilator", "Obliterator", "Devastator", "Vaporizer", "Nullifier",
		"Destroyer", "Enforcer", "Punisher", "Executioner", "Gun", "Turret", "Emitter", "Projector",
		"Igniter", "Repulsor", "Maw", "Render", "Bringer", "Cutter", "Screamer", "Howitzer",
	};

	const int m = clampi(customWeaponEditMode,  0, CUSTOM_WEAPON_MODES - 1);
	const int p = clampi(customWeaponEditLevel, 0, CUSTOM_POWER_LEVELS - 1);

	// Start from a random real weapon look (always valid), then perturb a little.
	if (customBulletPresetCount > 0)
	{
		const int idx = customRand(customBulletPresetCount);
		customWeaponImportLevel(idx, 1 + customRand(customBulletMaxPower(idx)));
	}
	else
	{
		makeDefaultWeapon(&customWeaponRaw[m][p]);
	}

	JE_WeaponType *w = &customWeaponRaw[m][p];
	w->shotrepeat = (JE_byte)clampi((int)w->shotrepeat + customRand(7) - 3, 1, 30);
	w->sound      = (JE_byte)(1 + customRand(CUSTOM_SOUND_MAX));

	snprintf(customWeaponName, sizeof(customWeaponName), "%s %s",
	         parts1[customRand(COUNTOF(parts1))], parts2[customRand(COUNTOF(parts2))]);
}

// Front and trailing-large mounts use the 2x2 sheet; the rest use single tiles.
static Sprite2_array *sidekickSheet(int mount)
{
	return (mount == 1 || mount == 2) ? &spriteSheet10 : &spriteSheet9;
}

int customSidekickSpriteCount(int mount)
{
	const Sprite2_array *s = sidekickSheet(mount);
	if (s->data == NULL || s->size < 2)
		return 0;   // sheet not loaded yet
	return SDL_SwapLE16(((const Uint16 *)s->data)[0]) / 2;   // offset table's first entry = #sprites * 2
}

// Synthesize an owner's custom sidekick (an options[] entry firing that owner's compiled
// weapon); rebuilt whenever the design changes.
static void customSidekickMaterialize(int owner, const CustomWeaponSlot *design)
{
	const int slot = customSidekickOwnerSlot[owner];
	if (slot <= 0 || slot > OPTION_NUM)
		return;

	// Reserve one mode-0 slot per charge stage.
	const int pwr = clampi(design->chargeStages - 1, 0, CUSTOM_POWER_LEVELS - 1);

	const int mount   = clampi(design->sidekickMount,     0, CUSTOM_SIDEKICK_MOUNTS - 1);
	const int frames  = clampi(design->sidekickFrames,    1, 20);
	const int step    = clampi(design->sidekickFrameStep, 0, 40);
	const int animate = clampi(design->sidekickAnimate,   1, 2);

	// Clamp 1-based body sprite reads, including charge frames and 2x2 mount offsets.
	const int count = customSidekickSpriteCount(mount);
	const int extra = (mount == 1 || mount == 2) ? 20 : 0;          // blit_sprite2x2 index+1/+19/+20
	const int hiIdx = (count > 0 && count - pwr - extra >= 1) ? count - pwr - extra : 1;
	const int base  = clampi(design->sidekickSprite, 1, hiIdx);

	JE_OptionType *o = &options[slot];
	memset(o, 0, sizeof(*o));
	SDL_strlcpy(o->name, design->name, sizeof(o->name));
	o->pwr         = (JE_byte)pwr;                   // 0 = instant fire; N = N more charge shots
	o->itemgraphic = 193;                            // valid shop icon
	o->cost        = (JE_word)clampi(design->cost, 0, 64000);
	o->tr          = (JE_byte)mount;                 // mount style (also selects the body sprite sheet)
	o->option      = (JE_byte)animate;               // 1 = animate while firing, 2 = always
	o->opspd       = 3;
	o->ani         = (JE_byte)frames;
	for (int f = 0; f < 20; ++f)                     // simple flip-book: base, base+step, base+2*step, ...
		o->gr[f] = (JE_word)clampi((f < frames) ? base + f * step : base, 1, hiIdx);
	o->wport       = (JE_byte)customWeaponOwnerPort[owner];  // fires through that owner's port ...
	o->wpnum       = (JE_word)customScratchSlot(owner, 0, 0);  // ... mode-0 level 1 (+ charge steps up)
	o->ammo        = 0;                              // infinite
	o->stop        = true;
	o->icongr      = 6;
}

static void customWeaponMaterializeOwner(int owner)
{
	if (owner < 0 || owner >= CUSTOM_WEAPON_OWNERS)
		return;

	const int port = customWeaponOwnerPort[owner];
	if (port <= 0 || port > PORT_NUM)
		return;

	CustomWeaponSlot *const design = &customWeaponOwnerDesign[owner];
	const int modes = clampi(design->modes, 1, CUSTOM_WEAPON_MODES);

	// Compile every (mode, level): the design already IS a weapon struct, so this is
	// a plain copy into its own scratch slot (kept separate from the source weapons).
	for (int m = 0; m < CUSTOM_WEAPON_MODES; ++m)
		for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
		{
			sanitizeRawWeapon(&design->raw[m][p]);
			weapons[customScratchSlot(owner, m, p)] = design->raw[m][p];
		}

	// Single-mode rear guns map both modes to the same reserved slots.
	SDL_strlcpy(weaponPort[port].name, design->name, sizeof(weaponPort[port].name));
	weaponPort[port].opnum = (JE_byte)modes;
	for (int p = 0; p < 11; ++p)
	{
		weaponPort[port].op[0][p] = (JE_word)customScratchSlot(owner, 0, p);
		weaponPort[port].op[1][p] = (JE_word)customScratchSlot(owner, modes >= 2 ? 1 : 0, p);
	}
	weaponPort[port].cost        = (JE_word)clampi(design->cost, 0, 64000);
	weaponPort[port].itemgraphic = (JE_word)clampi(design->itemGraphic, 1, 237);  // shop/HUD icon
	weaponPort[port].poweruse    = (JE_word)clampi(design->powerUse, 0, 255);

	customSidekickMaterialize(owner, design);  // keep the sidekick in sync with the weapon
	customWeaponOwnerDefined[owner] = true;
}

void customWeaponMaterialize(void)
{
	customWeaponModes = clampi(customWeaponModes, 1, CUSTOM_WEAPON_MODES);

	const int owner = customWeaponLocalOwner();
	customDesignStore(&customWeaponOwnerDesign[owner]);
	customWeaponMaterializeOwner(owner);

	// The editor edits in place, so take the sanitized weapons back.
	for (int m = 0; m < CUSTOM_WEAPON_MODES; ++m)
		for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
			customWeaponRaw[m][p] = customWeaponOwnerDesign[owner].raw[m][p];

	// Reset live fire cursors and cooldowns after replacing the compiled weapon.
	memset(shotMultiPos, 0, sizeof(shotMultiPos));
	memset(shotRepeat, 1, sizeof(shotRepeat));
}

int customWeaponScratchWeapon(int mode, int level)
{
	return customScratchSlot(customWeaponLocalOwner(), clampi(mode, 0, CUSTOM_WEAPON_MODES - 1),
	                         clampi(level, 0, CUSTOM_POWER_LEVELS - 1));
}

void customWeaponSelectLevel(int level)
{
	customWeaponEditLevel = clampi(level, 0, CUSTOM_POWER_LEVELS - 1);
}

void customWeaponSelectMode(int mode)
{
	customWeaponEditMode = clampi(mode, 0, CUSTOM_WEAPON_MODES - 1);
}

bool customWeaponEquip(void)
{
	if (!customWeaponEnabled || customWeaponPort <= 0 || customWeaponPort > PORT_NUM)
		return false;

	customWeaponMaterialize();

	Player *const this_player = &player[gameplay_local_player_index()];

	switch (customWeaponEquipSlot)
	{
	case CUSTOM_EQUIP_REAR:
		this_player->items.weapon[REAR_WEAPON].id = (Uint8)customWeaponPort;
		this_player->items.weapon[REAR_WEAPON].power = 1;
		break;
	case CUSTOM_EQUIP_LEFT:
		if (customSidekickSlot <= 0)
			return false;
		this_player->items.sidekick[LEFT_SIDEKICK] = (Uint8)customSidekickSlot;
		break;
	case CUSTOM_EQUIP_RIGHT:
		if (customSidekickSlot <= 0)
			return false;
		this_player->items.sidekick[RIGHT_SIDEKICK] = (Uint8)customSidekickSlot;
		break;
	case CUSTOM_EQUIP_BOTH:
		if (customSidekickSlot <= 0)
			return false;
		this_player->items.sidekick[LEFT_SIDEKICK]  = (Uint8)customSidekickSlot;
		this_player->items.sidekick[RIGHT_SIDEKICK] = (Uint8)customSidekickSlot;
		break;
	default:  // CUSTOM_EQUIP_FRONT
		this_player->items.weapon[FRONT_WEAPON].id = (Uint8)customWeaponPort;
		this_player->items.weapon[FRONT_WEAPON].power = 1;
		break;
	}
	return true;
}

// Import sources.

// Strip shop padding and a trailing "Ammo N" from picker names.
static void copyBulletName(char *dst, size_t dstsize, const char *src)
{
	if (dstsize == 0)
		return;

	while (*src == ' ' || *src == '\t')  // skip leading padding
		++src;
	size_t len = strlen(src);

	for (size_t i = 0; i + 5 <= len; ++i)  // cut a trailing standalone " Ammo[ <count>]" token
	{
		if (src[i] == ' ' && memcmp(&src[i + 1], "Ammo", 4) == 0 &&
		    (src[i + 5] == ' ' || src[i + 5] == '\0'))
		{
			len = i;
			break;
		}
	}

	while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t'))  // trim trailing
		--len;
	if (len > dstsize - 1)
		len = dstsize - 1;
	memcpy(dst, src, len);
	dst[len] = '\0';
}

static bool bulletNameSeen(const char *name)
{
	for (int j = 0; j < customBulletPresetCount; ++j)
		if (SDL_strcasecmp(customBulletPreset[j].name, name) == 0)
			return true;
	return false;
}

// Add one front/rear weapon port as an import source (remembers the port so the
// look can be sampled at any of its power levels / modes). Deduped by name.
static void addPortPreset(int port)
{
	if (customBulletPresetCount >= CUSTOM_BULLET_PRESET_MAX)
		return;

	const char *pn = weaponPort[port].name;
	if (pn[0] == '\0' || SDL_strcasecmp(pn, "None") == 0 || SDL_strcasecmp(pn, "Test") == 0)
		return;

	int maxLvl = 0;  // highest defined power level (1-based); skip a port with no weapons
	for (int p = 10; p >= 0; --p)
		if (weaponPort[port].op[0][p] > 0) { maxLvl = p + 1; break; }
	if (maxLvl == 0)
		return;

	char nm[31];
	copyBulletName(nm, sizeof(nm), pn);
	if (bulletNameSeen(nm))
		return;

	CustomBulletPreset *bp = &customBulletPreset[customBulletPresetCount++];
	strcpy(bp->name, nm);
	bp->sourcePort   = (JE_word)port;
	bp->sourceWeapon = 0;
	bp->sourceOption = 0;
	bp->maxPower     = (JE_byte)maxLvl;
}

// Import the complete shot ramp for charge sidekicks.
static void addOptionPreset(int opt)
{
	if (customBulletPresetCount >= CUSTOM_BULLET_PRESET_MAX)
		return;

	const char *on = options[opt].name;
	if (on[0] == '\0' || SDL_strcasecmp(on, "None") == 0)
		return;

	const int wn = options[opt].wpnum;
	if (wn <= 0 || wn > WEAP_NUM || weapons[wn].sg[0] == 0)
		return;

	char nm[31];
	copyBulletName(nm, sizeof(nm), on);
	if (bulletNameSeen(nm))
		return;

	CustomBulletPreset *bp = &customBulletPreset[customBulletPresetCount++];
	strcpy(bp->name, nm);
	bp->sourcePort   = 0;
	bp->sourceWeapon = (JE_word)wn;
	bp->sourceOption = (JE_word)opt;   // remember the option so Import can clone its body
	bp->maxPower     = (JE_byte)clampi(options[opt].pwr + 1, 1, CUSTOM_POWER_LEVELS);  // charge-shot count
}

static void buildBulletPresets(void)
{
	customBulletPresetCount = 0;

	for (int i = 1; i <= PORT_NUM; ++i)
		addPortPreset(i);

	for (int i = 1; i <= OPTION_NUM; ++i)
		addOptionPreset(i);
}

// Persistence.

// Serialize the fired bullet range plus any higher non-empty slots.
// This avoids writing every 255-wide per-bullet array for small designs.
static int usedBulletCount(const JE_WeaponType *w)
{
	int n = (w->multi > w->max) ? w->multi : w->max;
	if (n < 1) n = 1;
	if (n > CUSTOM_BULLETS_MAX) n = CUSTOM_BULLETS_MAX;
	for (int i = CUSTOM_BULLETS_MAX - 1; i >= n; --i)
		if (w->sg[i] || w->attack[i] || w->del[i] || w->sx[i] || w->sy[i] || w->bx[i] || w->by[i])
		{
			n = i + 1;
			break;
		}
	return n;
}

// Serialize one raw weapon to a compact, whitespace-free comma list of ints (parser-safe).
static void serializeRaw(const JE_WeaponType *w, char *buf, size_t bufSize)
{
	int o = 0;
	#define PUT(v) do { \
		if (o != 0 && o < (int)bufSize - 1) buf[o++] = ','; \
		int _r = snprintf(buf + o, (o < (int)bufSize) ? (size_t)(bufSize - o) : 0, "%d", (int)(v)); \
		if (_r > 0) o += _r; \
	} while (0)

	const int nb = usedBulletCount(w);   // only the populated slots (arrays are WEAPON_MULTI_MAX wide)

	PUT(w->drain); PUT(w->shotrepeat); PUT(w->multi); PUT(w->weapani);
	PUT(w->max); PUT(w->tx); PUT(w->ty); PUT(w->aim);
	for (int i = 0; i < nb; ++i) PUT(w->attack[i]);
	for (int i = 0; i < nb; ++i) PUT(w->del[i]);
	for (int i = 0; i < nb; ++i) PUT(w->sx[i]);
	for (int i = 0; i < nb; ++i) PUT(w->sy[i]);
	for (int i = 0; i < nb; ++i) PUT(w->bx[i]);
	for (int i = 0; i < nb; ++i) PUT(w->by[i]);
	for (int i = 0; i < nb; ++i) PUT(w->sg[i]);
	PUT(w->acceleration); PUT(w->accelerationx); PUT(w->circlesize);
	PUT(w->sound); PUT(w->trail); PUT(w->shipblastfilter);

	#undef PUT
}

// Parse a raw weapon back from serializeRaw()'s comma list, then clamp it to valid limits.
static void deserializeRaw(JE_WeaponType *w, const char *str)
{
	memset(w, 0, sizeof(*w));

	// Room for the widest blob: 8 leading scalars + 7 arrays x CUSTOM_BULLETS_MAX + 6 trailing scalars.
	long vals[14 + 7 * CUSTOM_BULLETS_MAX];
	int cnt = 0;
	while (*str != '\0' && cnt < (int)COUNTOF(vals))
	{
		char *end;
		long v = strtol(str, &end, 10);
		if (end == str)
			break;
		vals[cnt++] = v;
		str = end;
		while (*str == ',' || *str == ' ')
			++str;
	}

	// Serialized rows contain 14 scalars and seven arrays of width W.
	int width = (cnt >= 14) ? (cnt - 14) / 7 : CUSTOM_BULLETS_MAX;
	if (width < 1) width = 1;
	if (width > CUSTOM_BULLETS_MAX) width = CUSTOM_BULLETS_MAX;

	int i = 0;
	#define GET() ((i < cnt) ? vals[i++] : 0)

	w->drain = (JE_word)GET(); w->shotrepeat = (JE_byte)GET(); w->multi = (JE_byte)GET(); w->weapani = (JE_word)GET();
	w->max = (JE_byte)GET(); w->tx = (JE_byte)GET(); w->ty = (JE_byte)GET(); w->aim = (JE_byte)GET();
	for (int k = 0; k < width; ++k) w->attack[k] = (JE_byte)GET();
	for (int k = 0; k < width; ++k) w->del[k] = (JE_byte)GET();
	for (int k = 0; k < width; ++k) w->sx[k] = (JE_shortint)GET();
	for (int k = 0; k < width; ++k) w->sy[k] = (JE_shortint)GET();
	for (int k = 0; k < width; ++k) w->bx[k] = (JE_shortint)GET();
	for (int k = 0; k < width; ++k) w->by[k] = (JE_shortint)GET();
	for (int k = 0; k < width; ++k) w->sg[k] = (JE_word)GET();
	w->acceleration = (JE_shortint)GET(); w->accelerationx = (JE_shortint)GET(); w->circlesize = (JE_byte)GET();
	w->sound = (JE_byte)GET(); w->trail = (JE_byte)GET(); w->shipblastfilter = (JE_byte)GET();

	#undef GET

	sanitizeRawWeapon(w);
}

void customWeaponSerializeLevel(int mode, int level, char *buf, size_t bufSize)
{
	mode  = clampi(mode,  0, CUSTOM_WEAPON_MODES - 1);
	level = clampi(level, 0, CUSTOM_POWER_LEVELS - 1);
	serializeRaw(&customWeaponRaw[mode][level], buf, bufSize);
}

void customWeaponDeserializeLevel(int mode, int level, const char *str)
{
	mode  = clampi(mode,  0, CUSTOM_WEAPON_MODES - 1);
	level = clampi(level, 0, CUSTOM_POWER_LEVELS - 1);
	deserializeRaw(&customWeaponRaw[mode][level], str);
}

/* The online design wire format is little-endian and includes only populated bullet slots. */
typedef struct
{
	Uint8       *buf;
	size_t       cap, pos;
	bool         overflow;
}
CustomWireOut;

static void wirePutU8(CustomWireOut *w, int v)
{
	if (w->pos >= w->cap)
	{
		w->overflow = true;
		return;
	}
	w->buf[w->pos++] = (Uint8)(v & 0xff);
}

static void wirePutU16(CustomWireOut *w, int v)
{
	wirePutU8(w, v & 0xff);
	wirePutU8(w, (v >> 8) & 0xff);
}

static void wirePutU32(CustomWireOut *w, Uint32 v)
{
	wirePutU16(w, (int)(v & 0xffff));
	wirePutU16(w, (int)(v >> 16));
}

typedef struct
{
	const Uint8 *buf;
	size_t       len, pos;
	bool         truncated;
}
CustomWireIn;

static int wireGetU8(CustomWireIn *r)
{
	if (r->pos >= r->len)
	{
		r->truncated = true;
		return 0;
	}
	return r->buf[r->pos++];
}

static int wireGetS8(CustomWireIn *r)
{
	return (int)(Sint8)(Uint8)wireGetU8(r);
}

static int wireGetU16(CustomWireIn *r)
{
	const int lo = wireGetU8(r);
	return lo | (wireGetU8(r) << 8);
}

static Uint32 wireGetU32(CustomWireIn *r)
{
	const Uint32 lo = (Uint32)wireGetU16(r);
	return lo | ((Uint32)wireGetU16(r) << 16);
}

static void wirePutWeapon(CustomWireOut *w, const JE_WeaponType *weapon)
{
	const int nb = usedBulletCount(weapon);

	wirePutU8(w, nb);
	wirePutU16(w, weapon->drain);
	wirePutU8(w, weapon->shotrepeat);
	wirePutU8(w, weapon->multi);
	wirePutU16(w, weapon->weapani);
	wirePutU8(w, weapon->max);
	wirePutU8(w, weapon->tx);
	wirePutU8(w, weapon->ty);
	wirePutU8(w, weapon->aim);
	for (int i = 0; i < nb; ++i)
	{
		wirePutU8(w, weapon->attack[i]);
		wirePutU8(w, weapon->del[i]);
		wirePutU8(w, weapon->sx[i]);
		wirePutU8(w, weapon->sy[i]);
		wirePutU8(w, weapon->bx[i]);
		wirePutU8(w, weapon->by[i]);
		wirePutU16(w, weapon->sg[i]);
	}
	wirePutU8(w, weapon->acceleration);
	wirePutU8(w, weapon->accelerationx);
	wirePutU8(w, weapon->circlesize);
	wirePutU8(w, weapon->sound);
	wirePutU8(w, weapon->trail);
	wirePutU8(w, weapon->shipblastfilter);
}

static void wireGetWeapon(CustomWireIn *r, JE_WeaponType *weapon)
{
	memset(weapon, 0, sizeof(*weapon));

	const int nb = clampi(wireGetU8(r), 0, CUSTOM_BULLETS_MAX);
	weapon->drain      = (JE_word)wireGetU16(r);
	weapon->shotrepeat = (JE_byte)wireGetU8(r);
	weapon->multi      = (JE_byte)wireGetU8(r);
	weapon->weapani    = (JE_word)wireGetU16(r);
	weapon->max        = (JE_byte)wireGetU8(r);
	weapon->tx         = (JE_byte)wireGetU8(r);
	weapon->ty         = (JE_byte)wireGetU8(r);
	weapon->aim        = (JE_byte)wireGetU8(r);
	for (int i = 0; i < nb; ++i)
	{
		weapon->attack[i] = (JE_byte)wireGetU8(r);
		weapon->del[i]    = (JE_byte)wireGetU8(r);
		weapon->sx[i]     = (JE_shortint)wireGetS8(r);
		weapon->sy[i]     = (JE_shortint)wireGetS8(r);
		weapon->bx[i]     = (JE_shortint)wireGetS8(r);
		weapon->by[i]     = (JE_shortint)wireGetS8(r);
		weapon->sg[i]     = (JE_word)wireGetU16(r);
	}
	weapon->acceleration    = (JE_shortint)wireGetS8(r);
	weapon->accelerationx   = (JE_shortint)wireGetS8(r);
	weapon->circlesize      = (JE_byte)wireGetU8(r);
	weapon->sound           = (JE_byte)wireGetU8(r);
	weapon->trail           = (JE_byte)wireGetU8(r);
	weapon->shipblastfilter = (JE_byte)wireGetU8(r);

	sanitizeRawWeapon(weapon);
}

static size_t customWeaponSerializeSlot(const CustomWeaponSlot *design, Uint8 *buf, size_t cap)
{
	CustomWireOut w = { buf, cap, 0, false };

	wirePutU8(&w, CUSTOM_WEAPON_WIRE_VERSION);
	wirePutU8(&w, CUSTOM_WEAPON_MODES);
	wirePutU8(&w, CUSTOM_POWER_LEVELS);
	bool nameEnded = false;
	for (size_t i = 0; i < sizeof(design->name); ++i)
	{
		const Uint8 c = nameEnded ? 0 : (Uint8)design->name[i];
		wirePutU8(&w, c);
		nameEnded |= c == 0;
	}
	wirePutU16(&w, clampi(design->cost, 0, 64000));
	wirePutU16(&w, clampi(design->powerUse, 0, 255));
	wirePutU8(&w, clampi(design->equipSlot, 0, CUSTOM_EQUIP_COUNT - 1));
	wirePutU16(&w, clampi(design->itemGraphic, 1, 237));
	wirePutU8(&w, clampi(design->chargeStages, 1, CUSTOM_POWER_LEVELS));
	wirePutU8(&w, clampi(design->modes, 1, CUSTOM_WEAPON_MODES));
	wirePutU8(&w, clampi(design->sidekickMount, 0, CUSTOM_SIDEKICK_MOUNTS - 1));
	wirePutU16(&w, clampi(design->sidekickSprite, 1, 65535));
	wirePutU8(&w, clampi(design->sidekickFrames, 1, 20));
	wirePutU8(&w, clampi(design->sidekickFrameStep, 0, 40));
	wirePutU8(&w, clampi(design->sidekickAnimate, 1, 2));

	for (int m = 0; m < CUSTOM_WEAPON_MODES; ++m)
		for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
			wirePutWeapon(&w, &design->raw[m][p]);

	return w.overflow ? 0 : w.pos;
}

static bool customWeaponDeserializeSlot(CustomWeaponSlot *design, const Uint8 *buf, size_t len)
{
	if (design == NULL || buf == NULL || len < 4)
		return false;

	CustomWireIn r = { buf, len, 0, false };
	if (wireGetU8(&r) != CUSTOM_WEAPON_WIRE_VERSION)
		return false;

	const int modes  = clampi(wireGetU8(&r), 1, CUSTOM_WEAPON_MODES);
	const int levels = clampi(wireGetU8(&r), 1, CUSTOM_POWER_LEVELS);

	memset(design, 0, sizeof(*design));

	for (size_t i = 0; i < sizeof(design->name); ++i)
		design->name[i] = (char)wireGetU8(&r);
	design->name[sizeof(design->name) - 1] = '\0';

	design->cost              = clampi(wireGetU16(&r), 0, 64000);
	design->powerUse          = clampi(wireGetU16(&r), 0, 255);
	design->equipSlot         = clampi(wireGetU8(&r), 0, CUSTOM_EQUIP_COUNT - 1);
	design->itemGraphic       = clampi(wireGetU16(&r), 1, 237);
	design->chargeStages      = clampi(wireGetU8(&r), 1, CUSTOM_POWER_LEVELS);
	design->modes             = clampi(wireGetU8(&r), 1, CUSTOM_WEAPON_MODES);
	design->sidekickMount     = clampi(wireGetU8(&r), 0, CUSTOM_SIDEKICK_MOUNTS - 1);
	design->sidekickSprite    = clampi(wireGetU16(&r), 1, 65535);
	design->sidekickFrames    = clampi(wireGetU8(&r), 1, 20);
	design->sidekickFrameStep = clampi(wireGetU8(&r), 0, 40);
	design->sidekickAnimate   = clampi(wireGetU8(&r), 1, 2);

	// Leave modes or levels beyond the sender's layout blank.
	for (int m = 0; m < modes; ++m)
		for (int p = 0; p < levels; ++p)
			wireGetWeapon(&r, &design->raw[m][p]);

	// A short stream reads as zeros from the cut onwards, which is a blank weapon rather than
	// a wild index, but it is still not the design the peer flies. Refuse it.
	if (r.truncated || r.pos != len)
	{
		memset(design, 0, sizeof(*design));
		return false;
	}

	return true;
}

size_t customWeaponSerializeDesign(Uint8 *buf, size_t cap)
{
	// Clear fixed-width padding so identical designs produce identical bytes.
	static CustomWeaponSlot design;
	memset(&design, 0, sizeof(design));
	customDesignStore(&design);
	return customWeaponSerializeSlot(&design, buf, cap);
}

bool customWeaponAdoptDesign(int owner, const Uint8 *buf, size_t len)
{
	if (owner < 0 || owner >= CUSTOM_WEAPON_OWNERS)
		return false;

	CustomWeaponSlot *const design = &customWeaponOwnerDesign[owner];
	if (!customWeaponDeserializeSlot(design, buf, len))
		return false;

	customWeaponMaterializeOwner(owner);
	return true;
}

// Weapon library.

static void storeToSlot(int i)
{
	if (i < 0 || i >= CUSTOM_WEAPON_LIB_MAX)
		return;
	customDesignStore(&customWeaponLib[i]);
}

static void loadFromSlot(int i)
{
	if (i < 0 || i >= CUSTOM_WEAPON_LIB_MAX)
		return;
	customDesignLoad(&customWeaponLib[i]);
	customWeaponEditLevel = 0;   // open the switched-to weapon at level 1 / mode 1
	customWeaponEditMode  = 0;
}

size_t customWeaponSerializeLibrary(Uint8 *buf, size_t cap)
{
	if (buf == NULL || customWeaponLibCount < 1 || customWeaponLibCount > CUSTOM_WEAPON_LIB_MAX)
		return 0;

	customWeaponCurrentSlot = clampi(customWeaponCurrentSlot, 0, customWeaponLibCount - 1);
	storeToSlot(customWeaponCurrentSlot);

	CustomWireOut w = { buf, cap, 0, false };
	wirePutU8(&w, CUSTOM_WEAPON_LIBRARY_WIRE_VERSION);
	wirePutU8(&w, customWeaponLibCount);
	wirePutU8(&w, customWeaponCurrentSlot);
	wirePutU8(&w, 0);

	for (int i = 0; i < customWeaponLibCount && !w.overflow; ++i)
	{
		const size_t lengthAt = w.pos;
		wirePutU32(&w, 0);
		if (w.overflow)
			break;

		const size_t designLen = customWeaponSerializeSlot(&customWeaponLib[i],
		                                                      &w.buf[w.pos], w.cap - w.pos);
		if (designLen == 0 || designLen > UINT32_MAX)
		{
			w.overflow = true;
			break;
		}

		w.buf[lengthAt + 0] = (Uint8)(designLen & 0xff);
		w.buf[lengthAt + 1] = (Uint8)((designLen >> 8) & 0xff);
		w.buf[lengthAt + 2] = (Uint8)((designLen >> 16) & 0xff);
		w.buf[lengthAt + 3] = (Uint8)((designLen >> 24) & 0xff);
		w.pos += designLen;
	}

	return w.overflow ? 0 : w.pos;
}

static bool customWeaponDecodeLibrary(const Uint8 *buf, size_t len)
{
	if (buf == NULL || len < 4)
		return false;

	CustomWireIn r = { buf, len, 0, false };
	if (wireGetU8(&r) != CUSTOM_WEAPON_LIBRARY_WIRE_VERSION)
		return false;

	const int count = wireGetU8(&r);
	const int current = wireGetU8(&r);
	(void)wireGetU8(&r);  // reserved
	if (count < 1 || count > CUSTOM_WEAPON_LIB_MAX || current < 0 || current >= count)
		return false;

	// Static scratch avoids large console stack allocations. Decode fully before committing.
	static CustomWeaponSlot incoming[CUSTOM_WEAPON_LIB_MAX];
	for (int i = 0; i < count; ++i)
	{
		const Uint32 designLen = wireGetU32(&r);
		if (r.truncated || designLen > r.len - r.pos ||
		    !customWeaponDeserializeSlot(&incoming[i], &r.buf[r.pos], designLen))
			return false;
		r.pos += designLen;
	}
	if (r.truncated || r.pos != r.len)
		return false;

	memcpy(customWeaponLib, incoming, (size_t)count * sizeof(customWeaponLib[0]));
	customWeaponLibCount = count;
	customWeaponCurrentSlot = current;
	loadFromSlot(current);
	return true;
}

bool customWeaponAdoptLibrary(const Uint8 *buf, size_t len)
{
	if (!customWeaponDecodeLibrary(buf, len))
		return false;

	// Drop cached peer designs so the next session cannot reuse stale data.
	memset(customWeaponOwnerDefined, 0, sizeof(customWeaponOwnerDefined));
	customWeaponMaterialize();
	return true;
}

bool customWeaponRestoreLibrary(const Uint8 *buf, size_t len)
{
	if (!customWeaponDecodeLibrary(buf, len))
		return false;

	customWeaponMaterialize();
	return true;
}

// Fill a slot with the built-in default weapon (used for New and for unwritten slots).
static void slotSetDefault(CustomWeaponSlot *s)
{
	memset(s, 0, sizeof(*s));
	strcpy(s->name, "Custom Weapon");
	s->cost         = 5000;
	s->powerUse     = 3;
	s->equipSlot    = CUSTOM_EQUIP_FRONT;
	s->itemGraphic  = clampi(weaponPort[1].itemgraphic, 1, 237);
	s->chargeStages = 1;
	s->modes        = 1;
	s->sidekickMount     = 0;    // side pod
	s->sidekickSprite    = 87;   // Charge-Laser side-pod body frame
	s->sidekickFrames    = 1;
	s->sidekickFrameStep = 1;
	s->sidekickAnimate   = 1;
	for (int m = 0; m < CUSTOM_WEAPON_MODES; ++m)
		for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
			makeDefaultWeapon(&s->raw[m][p]);
}

void customWeaponSelectSlot(int slot)
{
	if (customWeaponLibCount <= 0)
		return;
	slot = clampi(slot, 0, customWeaponLibCount - 1);
	if (slot == customWeaponCurrentSlot)
		return;
	storeToSlot(customWeaponCurrentSlot);   // keep the working copy
	customWeaponCurrentSlot = slot;
	loadFromSlot(slot);
	customWeaponMaterialize();
}

int customWeaponLibraryNew(void)
{
	if (customWeaponLibCount >= CUSTOM_WEAPON_LIB_MAX)
		return -1;
	storeToSlot(customWeaponCurrentSlot);
	const int ni = customWeaponLibCount++;
	slotSetDefault(&customWeaponLib[ni]);
	snprintf(customWeaponLib[ni].name, sizeof(customWeaponLib[ni].name), "Weapon %d", ni + 1);
	customWeaponCurrentSlot = ni;
	loadFromSlot(ni);
	customWeaponMaterialize();
	return ni;
}

int customWeaponLibraryDuplicate(void)
{
	if (customWeaponLibCount >= CUSTOM_WEAPON_LIB_MAX)
		return -1;
	storeToSlot(customWeaponCurrentSlot);
	const int ni = customWeaponLibCount++;
	customWeaponLib[ni] = customWeaponLib[customWeaponCurrentSlot];   // copy the whole design
	// Mark the copy in its name if there's room.
	char *nm = customWeaponLib[ni].name;
	if (strlen(nm) <= sizeof(customWeaponLib[ni].name) - 6)
		strcat(nm, " Copy");
	customWeaponCurrentSlot = ni;
	loadFromSlot(ni);
	customWeaponMaterialize();
	return ni;
}

int customWeaponLibraryDelete(void)
{
	if (customWeaponLibCount <= 1)
		return -1;   // always keep at least one weapon
	for (int i = customWeaponCurrentSlot; i < customWeaponLibCount - 1; ++i)
		customWeaponLib[i] = customWeaponLib[i + 1];
	--customWeaponLibCount;
	if (customWeaponCurrentSlot >= customWeaponLibCount)
		customWeaponCurrentSlot = customWeaponLibCount - 1;
	loadFromSlot(customWeaponCurrentSlot);
	customWeaponMaterialize();
	return customWeaponCurrentSlot;
}

bool customWeaponLibrarySave(void)
{
	if (customWeaponLibCount < 1)           // never write an empty library (would lose the weapon)
	{
		customWeaponLibCount    = 1;
		customWeaponCurrentSlot = 0;
	}
	customWeaponCurrentSlot = clampi(customWeaponCurrentSlot, 0, customWeaponLibCount - 1);
	storeToSlot(customWeaponCurrentSlot);   // capture the working copy first

	FILE *f = dir_fopen(make_user_directory(), "custom_weapons.cfg", "w");
	if (f == NULL)
		return false;

	fprintf(f, "custom_weapons 1\n");
	fprintf(f, "count %d\n", customWeaponLibCount);
	fprintf(f, "current %d\n", customWeaponCurrentSlot);

	/* One raw-weapon blob, reused by this single-threaded writer. */
	static char blob[16384];
	for (int i = 0; i < customWeaponLibCount; ++i)
	{
		const CustomWeaponSlot *s = &customWeaponLib[i];
		fprintf(f, "weapon %d\n", i);
		fprintf(f, "name %s\n", s->name);
		fprintf(f, "props %d %d %d %d %d %d\n",
		        s->cost, s->powerUse, s->equipSlot, s->itemGraphic, s->chargeStages, s->modes);
		fprintf(f, "sidekick %d %d %d %d %d\n",
		        s->sidekickMount, s->sidekickSprite, s->sidekickFrames, s->sidekickFrameStep, s->sidekickAnimate);
		for (int m = 0; m < CUSTOM_WEAPON_MODES; ++m)
			for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
			{
				serializeRaw(&s->raw[m][p], blob, sizeof(blob));
				fprintf(f, "raw %d %d %s\n", m, p, blob);
			}
	}

	bool ok = ferror(f) == 0;
	if (fclose(f) != 0)
		ok = false;
	return ok;
}

// Load the whole library file. Called once from customWeaponInit (seeds a single slot from the
// working copy when no file exists yet).
static void customWeaponLibraryLoad(void)
{
	FILE *f = dir_fopen_warn(get_user_directory(), "custom_weapons.cfg", "r");
	if (f == NULL)
	{
		// No library file yet: seed a single slot from the current working copy.
		customWeaponLibCount    = 1;
		customWeaponCurrentSlot = 0;
		storeToSlot(0);
		return;
	}

	for (int i = 0; i < CUSTOM_WEAPON_LIB_MAX; ++i)   // valid defaults for any unwritten slot
		slotSetDefault(&customWeaponLib[i]);

	int count = 1, current = 0, cur = -1;
	// Must hold a whole "raw m p <blob>" line for the widest design. Loading is
	// single-threaded, so persistent scratch avoids a large console stack frame.
	static char line[16384];
	while (fgets(line, sizeof(line), f) != NULL)
	{
		if (strncmp(line, "count ", 6) == 0)
			count = clampi(atoi(line + 6), 1, CUSTOM_WEAPON_LIB_MAX);
		else if (strncmp(line, "current ", 8) == 0)
			current = atoi(line + 8);
		else if (strncmp(line, "weapon ", 7) == 0)
			cur = clampi(atoi(line + 7), 0, CUSTOM_WEAPON_LIB_MAX - 1);
		else if (cur >= 0 && strncmp(line, "name ", 5) == 0)
		{
			char *nm = line + 5;
			size_t len = strlen(nm);
			while (len > 0 && (nm[len - 1] == '\n' || nm[len - 1] == '\r'))
				nm[--len] = '\0';   // strip the newline
			SDL_strlcpy(customWeaponLib[cur].name, nm, sizeof(customWeaponLib[cur].name));
		}
		else if (cur >= 0 && strncmp(line, "props ", 6) == 0)
		{
			CustomWeaponSlot *s = &customWeaponLib[cur];
			const int parsed = sscanf(line + 6, "%d %d %d %d %d %d",
			       &s->cost, &s->powerUse, &s->equipSlot, &s->itemGraphic, &s->chargeStages, &s->modes);
			(void)parsed;
		}
		else if (cur >= 0 && strncmp(line, "sidekick ", 9) == 0)
		{
			CustomWeaponSlot *s = &customWeaponLib[cur];
			const int parsed = sscanf(line + 9, "%d %d %d %d %d",
			       &s->sidekickMount, &s->sidekickSprite, &s->sidekickFrames, &s->sidekickFrameStep, &s->sidekickAnimate);
			(void)parsed;
		}
		else if (cur >= 0 && strncmp(line, "raw ", 4) == 0)
		{
			int m = 0, p = 0, consumed = 0;
			if (sscanf(line + 4, "%d %d %n", &m, &p, &consumed) >= 2 &&
			    m >= 0 && m < CUSTOM_WEAPON_MODES && p >= 0 && p < CUSTOM_POWER_LEVELS)
				deserializeRaw(&customWeaponLib[cur].raw[m][p], line + 4 + consumed);
		}
	}
	fclose(f);

	customWeaponLibCount    = count;
	customWeaponCurrentSlot = clampi(current, 0, count - 1);
	loadFromSlot(customWeaponCurrentSlot);   // working copy <- the saved current weapon
}

// A weapon port is available if the item data left it as a placeholder, or if we already claimed
// it: a second claim pass runs against ports our own materialize has since renamed.
static bool customPortIsSpare(int port)
{
	if (weaponPort[port].name[0] == '\0' || SDL_strcasecmp(weaponPort[port].name, "Test") == 0)
		return true;

	for (int i = 0; i < CUSTOM_WEAPON_OWNERS; ++i)
		if (customWeaponOwnerPort[i] == port)
			return true;

	return false;
}

static bool customOptionIsSpare(int option)
{
	if (option == chargeLaserSlot)
		return false;
	if (strncmp(options[option].name, "None", 4) == 0)
		return true;

	for (int i = 0; i < CUSTOM_WEAPON_OWNERS; ++i)
		if (customSidekickOwnerSlot[i] == option)
			return true;

	return false;
}

/* Claim only placeholder ports and empty sidekick slots. See doc/notes.md#custom-weapons. */
static void customWeaponClaimSlots(void)
{
	int port = PORT_NUM;
	int option = OPTION_NUM;

	for (int owner = 0; owner < CUSTOM_WEAPON_OWNERS; ++owner)
	{
		int claimedPort = 0;
		for (; port >= 1 && claimedPort == 0; --port)
			if (customPortIsSpare(port))
				claimedPort = port;

		// The fallback still gives the owners distinct slots, so nothing aliases.
		customWeaponOwnerPort[owner] = (claimedPort != 0) ? claimedPort : PORT_NUM - owner;

		int claimedOption = 0;
		for (; option >= 1 && claimedOption == 0; --option)
			if (customOptionIsSpare(option))
				claimedOption = option;

		customSidekickOwnerSlot[owner] = claimedOption;
	}

	const int owner = customWeaponLocalOwner();
	customWeaponPort = customWeaponOwnerPort[owner];
	customSidekickSlot = customSidekickOwnerSlot[owner];
}

/* Item-data reloads clear reserved slots, so rebuild local and peer designs together. */
static void customWeaponMaterializeAll(void)
{
	const int local = customWeaponLocalOwner();
	customWeaponMaterialize();

	if (!isNetworkGame || !custom_ships_multiplayer_mode())
		return;

	for (int owner = 0; owner < CUSTOM_WEAPON_OWNERS; ++owner)
		if (owner != local && customWeaponOwnerDefined[owner])
			customWeaponMaterializeOwner(owner);
}

void customWeaponInit(void)
{
	buildBulletPresets();

	customWeaponEditLevel = clampi(customWeaponEditLevel, 0, CUSTOM_POWER_LEVELS - 1);
	customWeaponModes     = clampi(customWeaponModes,     1, CUSTOM_WEAPON_MODES);
	customWeaponEditMode  = clampi(customWeaponEditMode,  0, CUSTOM_WEAPON_MODES - 1);
	customWeaponChargeStages = clampi(customWeaponChargeStages, 1, CUSTOM_POWER_LEVELS);
	if (customWeaponEquipSlot < 0 || customWeaponEquipSlot >= CUSTOM_EQUIP_COUNT)
		customWeaponEquipSlot = CUSTOM_EQUIP_FRONT;

	// Sidekick body appearance (the sprite is additionally clamped to its sheet in materialize).
	customSidekickMount     = clampi(customSidekickMount,     0, CUSTOM_SIDEKICK_MOUNTS - 1);
	customSidekickFrames    = clampi(customSidekickFrames,    1, 20);
	customSidekickFrameStep = clampi(customSidekickFrameStep, 0, 40);
	customSidekickAnimate   = clampi(customSidekickAnimate,   1, 2);
	if (customSidekickSprite < 1)
		customSidekickSprite = 1;   // body sprite index is 1-based (blit reads offsetTable[index-1])

	// Default the shop icon to a real weapon's icon the first time (borrow port 1's).
	if (customWeaponItemGraphic < 1)
		customWeaponItemGraphic = clampi(weaponPort[1].itemgraphic, 1, 237);

	// Seed an empty library with a valid design.
	bool anyDefined = false;
	for (int m = 0; m < CUSTOM_WEAPON_MODES; ++m)
		for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
			if (customWeaponRaw[m][p].multi != 0)
				anyDefined = true;
	if (!anyDefined)
		for (int m = 0; m < CUSTOM_WEAPON_MODES; ++m)
			for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
				makeBlankWeapon(&customWeaponRaw[m][p]);

	// Read the library once; later item-data reloads only rebuild its reserved slots.
	static bool libraryLoaded = false;
	if (!libraryLoaded)
	{
		libraryLoaded = true;
		customWeaponLibraryLoad();
	}

	customWeaponClaimSlots();
	customWeaponMaterializeAll();
}

void customWeaponNetPrepare(void)
{
	customWeaponClaimSlots();
	customWeaponMaterializeAll();
}
