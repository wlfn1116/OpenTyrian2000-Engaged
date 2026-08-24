/* Custom Weapon Creator implementation. See custom_weapon.h. */

#include "custom_weapon.h"

#include "config.h"
#include "config_file.h"  // COMPILE_TIME_ASSERT
#include "episodes.h"
#include "file.h"
#include "network.h"
#include "player.h"
#include "sprite.h"

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

CustomWeaponSlot customWeaponLib[CUSTOM_WEAPON_LIB_MAX];
int              customWeaponLibCount    = 0;
int              customWeaponCurrentSlot = 0;

// inline so the [lo, hi] guarantee is visible at the call site: nearly every array index and loop
// bound in this file comes from here, and callers rely on it (baseMulti >= 1 is what keeps the
// `i % baseMulti` fan-out below from dividing by zero).
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

// Keep a raw design within the engine's hard limits (bullet count, sound index).
// Imported stock weapons are already valid; this mainly
// guards hand-edited, randomized, or config-loaded designs.
static void sanitizeRawWeapon(JE_WeaponType *w)
{
	w->multi = (JE_byte)clampi(w->multi, 1, CUSTOM_BULLETS_MAX);
	w->max   = (JE_byte)clampi(w->max,   1, CUSTOM_BULLETS_MAX);
	// shotrepeat 0 fires every tick. Stock lasers rely on it for a continuous beam.
	if (w->sound > CUSTOM_SOUND_MAX)
		w->sound = CUSTOM_SOUND_MAX;
}

// A blank one-bullet design (fires nothing until edited); what a fresh install
// starts with, matching a blank creator canvas.
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

	// Sidekick source: a charge sidekick's escalating shots sit at wpnum + 0..pwr, and
	// maxPower carries the shot count (pwr + 1). basePower selects the stage (1 =
	// uncharged). A non-charge sidekick has maxPower 1, so this is just wpnum.
	const int stages = clampi(bp->maxPower, 1, CUSTOM_POWER_LEVELS);
	return bp->sourceWeapon + clampi(basePower, 1, stages) - 1;
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

// Append src's bullet segments onto dst, up to CUSTOM_BULLETS_MAX, while keeping volley-wide fields.
// dst retains its fire rate, homing, spiral,
// sound, trail, etc.; only the per-bullet shape/motion arrays grow. Bullets past the cap are dropped.
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
		// Sidekick source (mode 0 only): combine each escalating charge shot onto the matching
		// power level, repeating the top stage past the source's range so every custom level gets
		// the look (a non-charge sidekick has one shot, which is added to all 11 levels).
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

void customWeaponAutoScaleLevels(void)
{
	const int m = clampi(customWeaponEditMode,  0, CUSTOM_WEAPON_MODES - 1);
	const int a = clampi(customWeaponEditLevel, 0, CUSTOM_POWER_LEVELS - 1);
	const JE_WeaponType anchor = customWeaponRaw[m][a];   // reference design (by value)
	const int baseMulti = clampi(anchor.multi, 1, CUSTOM_BULLETS_MAX);

	for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
	{
		if (p == a)
			continue;   // never touch the level the player actually tuned

		// Geometric power ramp about the anchor: exactly 1.0 at the anchor, weaker below and
		// stronger above (~15% per level). Done as a small loop so we don't pull in <math.h>.
		double mult = 1.0;
		const int steps = p - a;
		for (int s = 0; s < (steps >= 0 ? steps : -steps); ++s)
			mult = (steps >= 0) ? mult * 1.15 : mult / 1.15;

		JE_WeaponType w = anchor;   // start from the anchor, then scale the power axes

		// Damage per bullet; only real damage (1..98) scales; Ice (99), chain (101..249)
		// and piercing (>=250) are semantic codes, left exactly as designed.
		for (int i = 0; i < baseMulti; ++i)
		{
			const int at = anchor.attack[i];
			if (at >= 1 && at <= 98)
				w.attack[i] = (JE_byte)clampi((int)(at * mult + 0.5), 1, 98);
		}

		// Fire rate; more power fires faster, so the shot period (shotrepeat + 1) scales by
		// 1/mult. Keeps 0 ("every tick", a solid laser) reachable at the strong end.
		{
			const int period = (int)((anchor.shotrepeat + 1) / mult + 0.5);
			w.shotrepeat = (JE_byte)clampi(period - 1, 0, 255);
		}

		// Bullet count; more bullets at higher power. Shrink by keeping the centre-most
		// segments; grow by fanning out extra copies of the anchor's segments.
		const int target = clampi((int)(baseMulti * mult + 0.5), 1, CUSTOM_BULLETS_MAX);
		if (target < baseMulti)
		{
			const int start = (baseMulti - target) / 2;   // centred slice of the original fan
			for (int i = 0; start > 0 && i < target; ++i)
			{
				const int s = start + i;
				w.sg[i] = w.sg[s];  w.attack[i] = w.attack[s];  w.del[i] = w.del[s];
				w.sx[i] = w.sx[s];  w.sy[i]     = w.sy[s];
				w.bx[i] = w.bx[s];  w.by[i]     = w.by[s];
			}
		}
		else if (target > baseMulti)
		{
			for (int i = baseMulti; i < target; ++i)
			{
				const int s    = i % baseMulti;                     // reuse an original segment
				const int rank = (i - baseMulti) / baseMulti + 1;   // 1,1,..,2,2,..
				const int sign = ((i - baseMulti) & 1) ? 1 : -1;    // alternate sides
				OT_ASSUME(s >= 0 && s < CUSTOM_BULLETS_MAX);        // clampi bounds baseMulti to [1, MAX]
				w.sg[i] = anchor.sg[s];  w.attack[i] = w.attack[s];  w.del[i] = anchor.del[s];
				w.sx[i] = anchor.sx[s];  w.sy[i]     = anchor.sy[s];
				w.bx[i] = (JE_shortint)clampi(anchor.bx[s] + sign * rank * 8, -128, 127);
				w.by[i] = anchor.by[s];
			}
		}
		w.multi = (JE_byte)target;
		if (w.max < w.multi)
			w.max = w.multi;

		customWeaponRaw[m][p] = w;
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
	// Jump to the new top stage's design so its shot can be tuned right away. Its starting
	// content is whatever that power level already holds (a copy of the previous top when
	// the weapon was imported, else the default); non-destructive, edit it from here.
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

// The sprite sheet a mount style draws its body from: front (2) and trailing-large (1)
// use the 2x2 spriteSheet10, every other style the single-tile spriteSheet9 (mirrors the
// sidekick draw in mainint.c).
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

	// A charge sidekick fires wpnum + charge (charge in 0..pwr), so it needs a valid
	// weapon at each of those slots. chargeStages is the shot count (1..11), so
	// pwr = count - 1, and the top stage stays within this owner's mode-0 levels.
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

void customWeaponMaterializeOwner(int owner)
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

	// The reserved port: fire mode M, power level P (1..11) fires the matching slot.
	// A single-mode weapon points op[1] at the mode-0 slots so a rear-gun toggle is a
	// no-op instead of firing an undesigned bank.
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

// Copy a weapon/sidekick name for the picker, stripping the data's cosmetic
// shop formatting (leading/trailing padding and the " Ammo <count>" suffix that
// some sidekicks carry, e.g. "Phoenix Device Ammo 8" -> "Phoenix Device").
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

// Add one sidekick option as an import source. A non-charge sidekick is a single fixed
// weapon; a charge sidekick (option pwr > 0) has pwr+1 escalating shots at wpnum + 0..pwr,
// which maxPower records so its whole charge ramp can be imported.
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

	// Detect the per-bullet array width this blob was written with. The layout is 8 leading
	// scalars + 7 arrays of width W + 6 trailing scalars = 14 + 7*W integers, so W = (cnt - 14) /
	// 7.
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

bool customWeaponAdoptLibrary(const Uint8 *buf, size_t len)
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
	// Drop cached peer designs so the next session cannot reuse stale data.
	memset(customWeaponOwnerDefined, 0, sizeof(customWeaponOwnerDefined));
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

	FILE *f = dir_fopen(get_user_directory(), "custom_weapons.cfg", "w");
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

void customWeaponLibraryLoad(void)
{
	FILE *f = dir_fopen_warn(get_user_directory(), "custom_weapons.cfg", "r");
	if (f == NULL)
	{
		// No library file yet: seed a single slot from the current working copy. This also
		// migrates an old single custom weapon (loaded from opentyrian.cfg) into the library.
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
			// Trailing fields are absent in older files; whatever doesn't parse keeps its
			// slotSetDefault value, so a partial match is the expected case, not an error.
			const int parsed = sscanf(line + 6, "%d %d %d %d %d %d",
			       &s->cost, &s->powerUse, &s->equipSlot, &s->itemGraphic, &s->chargeStages, &s->modes);
			(void)parsed;
		}
		else if (cur >= 0 && strncmp(line, "sidekick ", 9) == 0)
		{
			CustomWeaponSlot *s = &customWeaponLib[cur];  // absent in old files -> keeps slotSetDefault values
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

/* Compile our own working copy plus any design the other player has published. Reloading the
 * item data wipes the reserved slots, and a network game does that at every level start, so the
 * peer's ship would otherwise be left holding an unbuilt placeholder weapon: a desync. */
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

	// If nothing has been loaded (fresh run, or a config with no custom weapon),
	// fill in a valid blank design (the editor's RESET restores the demo blaster
	// instead). A loaded design has multi >= 1 on at least one slot, so this
	// never clobbers it.
	bool anyDefined = false;
	for (int m = 0; m < CUSTOM_WEAPON_MODES; ++m)
		for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
			if (customWeaponRaw[m][p].multi != 0)
				anyDefined = true;
	if (!anyDefined)
		for (int m = 0; m < CUSTOM_WEAPON_MODES; ++m)
			for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
				makeBlankWeapon(&customWeaponRaw[m][p]);

	// Load the saved weapon library once. The working copy set up above seeds/overrides
	// the "current" slot; later re-inits (episode reloads) keep the in-memory library and
	// just re-materialize below.
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
