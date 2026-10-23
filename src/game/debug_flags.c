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
#include "debug_flags.h"

#include "config_file.h"
#include "network.h"
#include "opentyr.h"

JE_boolean  cheatInfiniteSidekickAmmo = false;
JE_boolean  cheatInfiniteShields = false;
JE_boolean  cheatInfiniteArmor = false;
JE_boolean  cheatInfiniteGenerator = false;  /* debug: weapons don't drain generator power */
JE_boolean  cheatNoEnemyFire = false;
JE_boolean  cheatInstantCharge = false;  /* debug: charge sidekicks reach full charge instantly */
JE_byte     noclipMode = NOCLIP_OFF;     /* debug: pass through enemies (see enum in debug_flags.h) */
JE_boolean  debugHitboxOverlay = false;
JE_boolean  debugPerfOverlay = false;
JE_boolean  autoFireSpecial = false;
JE_byte     debugTwiddleSpecial = 0;       /* debug: selected twiddle's special index (0 = none) */
JE_boolean  debugAutofireTwiddle = false;  /* debug: auto-fire the selected twiddle while fire is held */
JE_boolean  debugTwiddleTrigger = false;   /* debug: one-shot "fire the twiddle now" request from the menu */
JE_boolean  debugToggleFire = false;       /* debug: fire button toggles auto-fire instead of hold-to-fire */
JE_boolean  debugToggleFireActive = false; /* debug: the Toggle Fire latch; ship is currently auto-firing */
JE_byte     chargeSidekickAutofire = CHARGE_AUTOFIRE_ON;  /* default On; edited by the debug menu + the Weapons menu */
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
