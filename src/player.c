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
#include "player.h"

#include "endless.h"
#include "episodes.h"
#include "network.h"
#include "varz.h"  // hud_bars_dirty

Player player[2];

uint gameplay_local_player_index(void)
{
	return coop_mode_active() && isNetworkGame && thisPlayerNum >= 1 && thisPlayerNum <= 2
	     ? thisPlayerNum - 1 : 0;
}

/* Arcade life scaling derives its ceilings from each ship's stock hull and shield. SuperTyrian is
 * excluded; network games use the host's arcadeLifeBoost setting. */
bool arcade_life_scaling_active(void)
{
	return arcadeLifeBoost && arcade_rules_active() && !superTyrian;
}

/* In one-player arcade modes, rear-gun power combines its own pickups with lives - 1. Two-player
 * is excluded because player 2's rear-bay power is also the life counter. */
bool arcade_rear_scale_active(void)
{
	return arcadeRearGunScale && onePlayerAction && !twoPlayerMode && !superTyrian;
}

/* Effective bay power. Arcade rear scaling adds lives - 1 without changing stored pickup power. */
uint arcade_weapon_power(const Player *this_player, uint port)
{
	uint power = this_player->items.weapon[port].power;

	if (port == REAR_WEAPON && arcade_rear_scale_active())
	{
		// Player 2's rear-bay power aliases the life counter.
		const Uint8 *const life_counter = this_player->lives;

		if (life_counter != &this_player->items.weapon[port].power && *life_counter > 1)
			power += *life_counter - 1;
	}

	if (power < 1)
		power = 1;
	else if (power > 11)  // the same ceiling power_up_weapon enforces
		power = 11;

	return power;
}

/* Linear from `base` at 1 life to a full bar at ARCADE_LIVES_MAX.  Integer throughout: this runs
 * inside the simulation, so both machines in a network game must land on the same byte. */
static uint arcade_scaled_max(uint base, uint lives)
{
	if (base == 0 || base >= ARCADE_FULL_BAR || !arcade_life_scaling_active())
		return base;  // no shield fitted / already a full bar / not an arcade game

	if (lives < 1)
		lives = 1;
	else if (lives > ARCADE_LIVES_MAX)
		lives = ARCADE_LIVES_MAX;

	const uint steps = ARCADE_LIVES_MAX - 1;
	return base + ((ARCADE_FULL_BAR - base) * (lives - 1) + steps / 2) / steps;
}

uint arcade_armor_max(const Player *this_player)
{
	return arcade_scaled_max(this_player->hull_armor, *this_player->lives);
}

uint arcade_shield_max(const Player *this_player)
{
	return arcade_scaled_max(shields[this_player->items.shield].mpwr * 2, *this_player->lives);
}

/* Carry a live gauge across a change of maximum, preserving how damaged it was. */
static uint arcade_carry_gauge(uint current, uint old_max, uint new_max)
{
	if (old_max == 0)
		return new_max;
	if (current > old_max)
		current = old_max;

	return (current * new_max + old_max / 2) / old_max;
}

/* Keep this mode-independent so disabling the tweak also restores inflated ceilings. */
void arcade_rescale_to_lives(Player *this_player)
{
	bool changed = false;

	const uint armor_max = arcade_armor_max(this_player);
	if (this_player->initial_armor != armor_max)
	{
		// Preserve the damage ratio when the maximum changes.
		this_player->armor = arcade_carry_gauge(this_player->armor, this_player->initial_armor, armor_max);
		this_player->initial_armor = armor_max;
		changed = true;
	}

	const uint shield_max = arcade_shield_max(this_player);
	if (this_player->shield_max != shield_max)
	{
		this_player->shield = arcade_carry_gauge(this_player->shield, this_player->shield_max, shield_max);
		this_player->shield_max = shield_max;
		changed = true;
	}

	// Defer repainting to the rollback-safe tick poll in tyrian2.c.
	if (changed)
		hud_bars_dirty = true;
}

void calc_purple_balls_needed(Player *this_player)
{
	static const uint purple_balls_required[12] = { 1, 1, 2, 4, 8, 12, 16, 20, 25, 30, 40, 50 };
	
	this_player->purple_balls_needed = purple_balls_required[*this_player->lives];
}

bool coopSharedCredit = true;
static bool coop_session_shared_credit = true;

void coop_set_session_shared_credit(bool shared)
{
	coop_session_shared_credit = shared;
}

bool coop_credit_is_shared(void)
{
	return coop_mode_active() && coop_session_shared_credit;
}

// Credit earned cash. Player 1's Endless income must pass through the run ledger; Online
// Campaign's Shared credit pays the full amount to both players instead of to one.
static void player_credit_cash(Player *this_player, long amount, EndlessCashSource endless_source)
{
	if (coop_credit_is_shared())
	{
		for (uint i = 0; i < COUNTOF(player); ++i)
			player[i].cash += amount;
		return;
	}

	if (endlessMode && this_player == &player[0])
		endlessCashCredit(amount, endless_source);
	else
		this_player->cash += amount;
}

void player_award_pickup_cash(Player *this_player, long amount)
{
	player_credit_cash(this_player, amount, ENDLESS_CASH_PICKUP);
}

void player_award_kill_cash(Player *this_player, long amount)
{
	player_credit_cash(this_player, amount, ENDLESS_CASH_KILL);
}

bool power_up_weapon(Player *this_player, uint port)
{
	const bool can_power_up = this_player->items.weapon[port].id != 0 &&  // not None
	                          this_player->items.weapon[port].power < 11; // not at max power
	if (can_power_up)
	{
		++this_player->items.weapon[port].power;
		shotMultiPos[port] = 0; // shared per-port firing cursor
		if (coop_mode_active())
			this_player->shot_multi_pos[port] = 0;

		calc_purple_balls_needed(this_player);
		arcade_rescale_to_lives(this_player);  // this port IS the life counter in arcade modes
	}
	else  // cash consolation prize
	{
		player_award_pickup_cash(this_player, 1000);
	}
	
	return can_power_up;
}

void handle_got_purple_ball(Player *this_player)
{
	if (this_player->purple_balls_needed > 1)
		--this_player->purple_balls_needed;
	else
		power_up_weapon(this_player, this_player->is_dragonwing ? REAR_WEAPON : FRONT_WEAPON);
}
