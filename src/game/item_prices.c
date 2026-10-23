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
#include "item_prices.h"

#include "config.h"
#include "endless.h"
#include "episodes.h"
#include "game_menu.h"
#include "game_state.h"
#include "lvlmast.h"
#include "opentyr.h"
#include "player.h"

#include <assert.h>

Sint64 upgradeCost;
Sint64 downgradeCost;

static Sint64 JE_getValue(JE_byte itemType, JE_word itemNum);

// cost to upgrade a weapon power from power-1 (where power == 0 indicates an unupgraded weapon)
Sint64 weapon_upgrade_cost(Sint64 base_cost, unsigned int power)
{
	assert(power <= 11);

	unsigned int temp = 0;

	// 0 1 3 6 10 15 21 29 ...
	for (; power > 0; power--)
		temp += power;

	return base_cost * temp;
}

/* Prices are Sint64 like the wallets they are checked against; the widest product here (a 16-bit
 * base cost, the power ladder, the expert and endless multipliers) stays far below 2^63. */
Sint64 JE_getCost(JE_byte itemType, JE_word itemNum)
{
	Sint64 cost = 0;

	switch (itemType)
	{
	case 2:
		// ships[] stops at SHIP_DRAGONWING, but only ids above 90 are extra ships. Test the
		// array's own bound, not the extra-ship one, or an id in between reads past the end.
		cost = (itemNum > SHIP_DRAGONWING) ? 100 : ships[itemNum].cost;
		break;
	case 3:
	case 4:
		cost = weaponPort[itemNum].cost;

		const uint port = itemType - 3,
			item_power = player[JE_shopPlayerIndex()].items.weapon[port].power - 1;

		downgradeCost = weapon_upgrade_cost(cost, item_power);
		upgradeCost = weapon_upgrade_cost(cost, item_power + 1);
		break;
	case 5:
		cost = shields[itemNum].cost;
		break;
	case 6:
		cost = powerSys[itemNum].cost;
		break;
	case 7:
	case 8:
		cost = options[itemNum].cost;
		break;
	}

	if (expertMode)
	{
		// purchase price scales with the shop knob; power upgrades with their own knob
		cost = cost * expertShopCostMult;

		if (itemType == 3 || itemType == 4)
		{
			downgradeCost = downgradeCost * expertUpgradeCostMult;
			upgradeCost = upgradeCost * expertUpgradeCostMult;
		}
	}

	if (endlessMode)
	{
		// Endless: shop prices inflate with depth (+19%/level, capped 100x) since income also
		// scales; the first five zones ramp at half slope.
		int pct;
		if (endlessRunDepth < 5)
			pct = 100 + endlessRunDepth * 19 / 2;                 // first 5 zones: gentle half-slope
		else
			pct = 100 + 4 * 19 / 2 + (endlessRunDepth - 4) * 19;  // then full +19%/depth (continuous at zone 5)
		if (pct > 10000)
			pct = 10000;                            // cap at 100x
		pct += endlessShopTaxPercent();             // Loan Shark: a permanent debt tax on top of the depth cap
		if (endlessActiveMods & ENDLESS_MOD_FAVOR)  // Merchant's Favor: the outpost slashes prices
			pct = pct * 65 / 100;
		// Financier perk: better terms, in basis points because the per-stack cut isn't a whole
		// percent. Multiplies like Favor does, so the two compound instead of one overriding the other.
		pct = pct * endlessPerkShopCostBp() / 10000;

		cost = cost * pct / 100;

		if (itemType == 3 || itemType == 4)
		{
			downgradeCost = downgradeCost * pct / 100;
			upgradeCost   = upgradeCost * pct / 100;
		}
	}

	return cost;
}

Sint64 JE_totalScore(const Player *this_player)
{
	Sint64 temp = this_player->cash;

	temp += JE_getValue(2, this_player->items.ship);
	temp += JE_getValue(3, this_player->items.weapon[FRONT_WEAPON].id);
	temp += JE_getValue(4, this_player->items.weapon[REAR_WEAPON].id);
	temp += JE_getValue(5, this_player->items.shield);
	temp += JE_getValue(6, this_player->items.generator);
	temp += JE_getValue(7, this_player->items.sidekick[LEFT_SIDEKICK]);
	temp += JE_getValue(8, this_player->items.sidekick[RIGHT_SIDEKICK]);

	return temp;
}

static Sint64 JE_getValue(JE_byte itemType, JE_word itemNum)
{
	Sint64 value = 0;

	switch (itemType)
	{
	case 2:
		// Extra ships (id above 90) have no shop row and sell for nothing.
		if (itemNum <= SHIP_DRAGONWING)
			value = ships[itemNum].cost;
		break;
	case 3:
	case 4:;
		const Sint64 base_value = weaponPort[itemNum].cost;

		const uint port = itemType - 3;
		const uint item_power = player[JE_shopPlayerIndex()].items.weapon[port].power - 1;

		value = base_value;
		for (unsigned int i = 1; i <= item_power; ++i)
			value += weapon_upgrade_cost(base_value, i);
		break;
	case 5:
		value = shields[itemNum].cost;
		break;
	case 6:
		value = powerSys[itemNum].cost;
		break;
	case 7:
	case 8:
		value = options[itemNum].cost;
		break;
	}

	return value;
}
