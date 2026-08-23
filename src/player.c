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
#include "player.h"

#include "endless.h"
#include "episodes.h"
#include "network.h"
#include "varz.h"  // hud_bars_dirty

Player player[2];

uint gameplay_local_player_index(void)
{
	// Every dual-ship session gives each machine a ship of its own to read: both co-op modes and
	// Separate arcade. The linked arcade pair shares one HUD sidebar, so it stays on ship one.
	return dual_ship_mode() && isNetworkGame && thisPlayerNum >= 1 && thisPlayerNum <= 2
	     ? thisPlayerNum - 1 : 0;
}

/* The Super Arcade ship this ship flies (1..SA), or SA_NONE. Online Super Arcade lets each player
 * pick their own, so every SAWeapon / SASpecialWeapon read has to be per ship rather than off the
 * session-wide superArcadeMode. Clamped: the value rides the save record and the wire. */
uint player_sa_ship(const Player *this_player)
{
	const uint sa = this_player->items.super_arcade_mode;
	return (sa >= 1 && sa <= SA) ? sa : (uint)SA_NONE;
}

/* Resolve a Super Arcade color in the collecting ship's arsenal. Plain-game
 * scripted balls retain the original first-ship fallback. */
uint player_sa_ball_weapon(const Player *this_player, uint slot)
{
	// Script-spawned values are not guaranteed to name a real colour slot.
	if (slot >= COUNTOF(SAWeapon[0]))
		slot = COUNTOF(SAWeapon[0]) - 1;

	const uint sa_ship = player_sa_ship(this_player);
	return SAWeapon[(sa_ship != SA_NONE ? sa_ship : 1u) - 1][slot];
}

/* Return the weapon-power byte used as this ship's life counter. All bindings,
 * including rollback restore, must use the same arcade ownership rule. */
uint player_lives_port(uint p)
{
	if (dual_ship_mode())
		return FRONT_WEAPON;
	return (p < COUNTOF(player)) ? p : FRONT_WEAPON;
}

/* Arcade life scaling derives its ceilings from each ship's stock hull and shield. SuperTyrian is
 * excluded; network games use the host's arcadeLifeBoost setting. */
bool arcade_life_scaling_active(void)
{
	return arcadeLifeBoost && arcade_rules_active() && !superTyrian;
}

/* Rear-gun power combines its own pickups with lives - 1. The linked pair is excluded because
 * player two's rear-bay power is also its life counter; Separate arcade counts lives on each
 * ship's own front gun (player_lives_port), so its rear bay is free to scale. */
bool arcade_rear_scale_active(void)
{
	if (!arcadeRearGunScale || superTyrian)
		return false;
	return (onePlayerAction && !twoPlayerMode) || arcade_separate_mode();
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
uint player_carry_gauge(uint current, uint old_max, uint new_max)
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
		this_player->armor = player_carry_gauge(this_player->armor, this_player->initial_armor, armor_max);
		this_player->initial_armor = armor_max;
		changed = true;
	}

	const uint shield_max = arcade_shield_max(this_player);
	if (this_player->shield_max != shield_max)
	{
		this_player->shield = player_carry_gauge(this_player->shield, this_player->shield_max, shield_max);
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

	// The alias behind lives is a weapon-power byte, which hostile save records can set past
	// the table; clamp rather than read whatever happens to sit after it.
	uint lives = *this_player->lives;
	if (lives >= COUNTOF(purple_balls_required))
		lives = COUNTOF(purple_balls_required) - 1;
	this_player->purple_balls_needed = purple_balls_required[lives];
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

// Lobby preference for Online Arcade; the session flag it arms is arcadeSeparateMode (config.h).
bool arcadeSeparateShips = false;

bool coopDoubleEarnings = false;
static bool coop_session_double_earnings = false;

void coop_set_session_double_earnings(bool on)
{
	coop_session_double_earnings = on;
}

bool coop_earnings_are_doubled(void)
{
	// Never under Shared: both players already take every payment at its full value there.
	return coop_mode_active() && coop_session_double_earnings && !coop_credit_is_shared();
}

// Credit earned cash. This machine's own Endless income must pass through the run ledger; Online
// Campaign's Shared credit pays the full amount to both players instead of to one.
static void player_credit_cash(Player *this_player, Sint64 amount, EndlessCashSource endless_source)
{
	if (coop_credit_is_shared())
	{
		/* Both wallets earn the full amount. This machine's own share still goes through the
		 * run ledger, or an Endless run books every shared payment as undeclared drift: the
		 * audit warns, and the summary files the lot under "other" instead of what earned it. */
		for (uint i = 0; i < COUNTOF(player); ++i)
		{
			if (endlessMode && i == endlessEconomyIndex())
				endlessCashCredit(amount, endless_source);
			else
				player_add_cash(&player[i], amount);
		}
		return;
	}

	// The ledger tracks the wallet of whoever is sitting at this keyboard, so the gate has to name
	// that same ship. Naming player 1 outright meant the joiner booked its partner's earnings into
	// its own wallet and paid its own earnings straight past the ledger. Solo, the two are one.
	if (endlessMode && this_player == &player[endlessEconomyIndex()])
		endlessCashCredit(amount, endless_source);
	else
		player_add_cash(this_player, amount);
}

void player_award_pickup_cash(Player *this_player, Sint64 amount)
{
	if (coop_earnings_are_doubled())
		amount *= 2;
	player_credit_cash(this_player, amount, ENDLESS_CASH_PICKUP);
}

void player_award_kill_cash(Player *this_player, Sint64 amount)
{
	// Double Earnings covers combat income whole: kills and the bounties built on them, not
	// only pickups. Zone bonuses and bank interest stay at face value.
	if (coop_earnings_are_doubled())
		amount *= 2;
	player_credit_cash(this_player, amount, ENDLESS_CASH_KILL);
}

void player_award_bounty_cash(Player *this_player, Sint64 amount)
{
	// A bounty is kill cash under its own ledger row, so the run summary can name it.
	if (coop_earnings_are_doubled())
		amount *= 2;
	player_credit_cash(this_player, amount, ENDLESS_CASH_BOUNTY);
}

bool power_up_weapon(Player *this_player, uint port)
{
	const bool can_power_up = this_player->items.weapon[port].id != 0 &&  // not None
	                          this_player->items.weapon[port].power < 11; // not at max power
	if (can_power_up)
	{
		++this_player->items.weapon[port].power;
		shotMultiPos[port] = 0; // shared per-port firing cursor
		if (dual_ship_mode())
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
