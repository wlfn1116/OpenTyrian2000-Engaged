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
#ifndef DEBUG_FLAGS_H
#define DEBUG_FLAGS_H

#include "opentyr.h"

extern JE_boolean cheatInfiniteSidekickAmmo;
extern JE_boolean cheatInfiniteShields;
extern JE_boolean cheatInfiniteArmor;
extern JE_boolean cheatInfiniteGenerator;  /* debug: weapons don't drain generator power */
extern JE_boolean cheatNoEnemyFire;  /* debug: suppress enemy projectiles */
extern JE_boolean cheatInstantCharge;  /* debug: charge sidekicks reach full charge instantly */

// Noclip debug mode (cycled in the debug menu): pass through enemies without
// collision. The transparent state additionally draws the ship semi-transparent.
enum
{
	NOCLIP_OFF         = 0,  // normal collision
	NOCLIP_ON          = 1,  // pass through enemies, ship drawn normally
	NOCLIP_TRANSPARENT = 2,  // pass through enemies, ship drawn semi-transparent
	NOCLIP_NUM         = 3,
};
extern JE_byte noclipMode;

extern JE_boolean debugHitboxOverlay;  /* debug: draw collision boxes */
extern JE_boolean debugPerfOverlay;    /* debug: draw FPS/timing/counts overlay */
extern JE_boolean autoFireSpecial;
extern JE_byte    debugTwiddleSpecial;      /* debug: selected twiddle's special index (0 = none) */
extern JE_boolean debugAutofireTwiddle;     /* debug: auto-fire the selected twiddle while fire is held */
extern JE_boolean debugTwiddleTrigger;      /* debug: one-shot fire request from the debug menu */
extern JE_boolean debugToggleFire;          /* debug: fire button toggles auto-fire on/off */
extern JE_boolean debugToggleFireActive;    /* debug: Toggle Fire latch; currently auto-firing */

// "Autofire Charge Sidekicks" mode (cycled in the debug menu). Governs whether a
// charge sidekick (pwr > 0) fires while the main fire button is held.
enum
{
	CHARGE_AUTOFIRE_OFF  = 0,  // only fires on its own dedicated sidekick button
	CHARGE_AUTOFIRE_ON   = 1,  // autofires while the main fire button is held
	CHARGE_AUTOFIRE_FULL = 2,  // autofires on the main button only when fully charged
	CHARGE_AUTOFIRE_FAST = 3,  // like ON, but reloads at the fastest charge stage's shotrepeat;
	                           // paired with Instant Charge this fires full-power shots at top speed
	CHARGE_AUTOFIRE_NUM  = 4,  // (append new modes; the value is saved per slot; don't renumber)
};
extern JE_byte    chargeSidekickAutofire;
extern JE_boolean difficultyAdjust;
extern JE_boolean expertMode;

// Expert-mode defaults, adjustable from the Expert Settings debug menu.
#define EXPERT_DEF_BOSS_HP      3    /* boss HP multiplier (x)            */
#define EXPERT_DEF_ENEMY_ARMOR  125  /* regular enemy armor scaling (%)   */
#define EXPERT_DEF_ENERGY       150  /* weapon energy use (%)             */
#define EXPERT_DEF_SHOP_COST    2    /* shop purchase cost multiplier (x) */
#define EXPERT_DEF_UPGRADE_COST 2    /* weapon upgrade cost multiplier (x)*/
#define EXPERT_DEF_CASH         150  /* cash earned from enemies (%)      */

extern int expertBossHpMult;     /* divisor: bosses take 1/N damage -> Nx HP */
extern int expertEnemyArmorPct;  /* >=100; scales non-boss enemy armor       */
extern int expertEnergyPct;      /* >=100; scales weapon energy use          */
extern int expertShopCostMult;   /* >=1; scales shop purchase prices         */
extern int expertUpgradeCostMult;/* >=1; scales weapon power upgrade prices  */
extern int expertScorePct;       /* >=100; scales cash earned from enemies   */

/* Shared descriptor for the expert tunables: one source of truth driving the
 * debug submenu, the config load/save, and reset-to-defaults. */
typedef struct
{
	const char* label;   /* submenu label                  */
	const char* cfgKey;  /* opentyrian.cfg option key      */
	int*        value;   /* the live global                */
	int         lo, hi;  /* inclusive adjustable range     */
	int         step;    /* adjust increment               */
	int         def;     /* default value                  */
	char        fmt;     /* 'x' multiplier or '%' percent  */
}
ExpertSetting;

extern ExpertSetting expertSettings[];
extern const int expertSettingsCount;

void clamp_expert_settings(void);  /* clamp each tunable into [lo, hi] */

#endif /* DEBUG_FLAGS_H */
