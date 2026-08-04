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
#include "varz.h"  // hud_bars_dirty

Player player[2];

/* Arcade only, Super Arcade secret ships included.  Nothing here is per-ship: both ceilings are
 * derived from whatever hull and shield the ship itself was shipped with, so the secret ships need
 * no table of their own -- the U-Ship climbs from its 8 hull and the Nort Ship is already past a
 * full bar at 30 and simply stays there.  SuperTyrian stays out: it is a single fixed loadout
 * balanced around the Stalker 21.126, not a ship you pick.  `arcadeLifeBoost` is the Game Tweaks
 * row; in a network game the host's copy of it binds the session (network.c). */
bool arcade_life_scaling_active(void)
{
	return arcadeLifeBoost && (onePlayerAction || twoPlayerMode) && !superTyrian;
}

/* Rear-gun scaling.  In the arcade modes a life count IS a weapon power level -- player 1's front
 * bay, player 2's rear bay (`lives = &weapon[p].power`, JE_initPlayerData) -- so a 1P arcade front
 * gun climbs with every extra life while the rear gun sits at whatever level its own power-up balls
 * left it at, which is level 1 for most of a run.  With the Arcade row on, the life count feeds the
 * rear gun too, on top of whatever its own pickups have banked.
 *
 * ONE-PLAYER only -- 1P Arcade and the Super Arcade secret ships -- and not SuperTyrian (ENGAGE),
 * which flies one fixed scripted loadout.  Two-player is deliberately out rather than merely inert:
 * there the rear bay IS player 2's life counter, so "add the life count to the rear gun" would be
 * adding a number to itself.  (Player 1's rear bay does not fire in a 2P game either -- the firing
 * loop in JE_playerMovement gives each ship one bay -- so nothing is lost by excluding the mode.)
 * The twoPlayerMode test matters even though onePlayerAction is set: galaga mode raises
 * twoPlayerMode mid-level when the dragonwing spawns, with onePlayerAction still true. */
bool arcade_rear_scale_active(void)
{
	return arcadeRearGunScale && onePlayerAction && !twoPlayerMode && !superTyrian;
}

/* The power level a bay actually fires at.  Everything except a scaled arcade rear gun just reads
 * its own stored power.
 *
 * The scaled rear gun STACKS the two sources: its stored power is the base (level 1 plus one for
 * every rear power-up ball the run has banked) and the life count adds `lives - 1` on top.  So a
 * rear ball is always worth a level rather than vanishing under a bigger life count, and because
 * the life term can only ever add, the gun never fires below its own stored power -- a death costs
 * the life it took and nothing that was paid for with pickups.
 *
 * Nothing is written back: the stored power keeps accumulating on its own, so the row can be
 * flipped mid-run and the gun that was earned is still there when it goes off again. */
uint arcade_weapon_power(const Player *this_player, uint port)
{
	uint power = this_player->items.weapon[port].power;

	if (port == REAR_WEAPON && arcade_rear_scale_active())
	{
		// Never scale a bay that IS its owner's life counter: `lives` aliases weapon[p].power
		// (JE_initPlayerData), so for player 2 that bay is the rear one and the line below would
		// be adding the number to itself.  The 1P-only gate above already rules that out; the
		// check keeps the arithmetic correct on its own terms if the gate is ever widened.
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

/* No arcade guard here on purpose: with scaling off both ceilings evaluate to the plain hull and
 * `mpwr * 2` that the rest of the game already holds, so this is a no-op in the campaign and in
 * endless -- and switching the Game Tweaks row off still walks an inflated ceiling back down. */
void arcade_rescale_to_lives(Player *this_player)
{
	bool changed = false;

	const uint armor_max = arcade_armor_max(this_player);
	if (this_player->initial_armor != armor_max)
	{
		// Proportional, not absolute: a life gained must not hand out free hull, and a life lost
		// must not leave the gauge reading over its own maximum.
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

	// Both gauges are event-painted, and a life picked up mid-level paints neither -- the new
	// ceilings would sit unshown until the next hit or shield tick. Flag it instead of drawing:
	// the tick's repaint poll (tyrian2.c) is the one place that is safe during a rollback resim.
	if (changed)
		hud_bars_dirty = true;
}

void calc_purple_balls_needed(Player *this_player)
{
	static const uint purple_balls_required[12] = { 1, 1, 2, 4, 8, 12, 16, 20, 25, 30, 40, 50 };
	
	this_player->purple_balls_needed = purple_balls_required[*this_player->lives];
}

// Credit cash that fell out of the playfield to whoever collected it. Only player 1's share is a
// run's earnings, so in endless that goes through the ledger; every other case is a plain credit.
// Use this instead of `this_player->cash += n` for anything picked up, or the endless run-over tally
// files it under "untagged".
void player_award_pickup_cash(Player *this_player, long amount)
{
	if (endlessMode && this_player == &player[0])
		endlessCashCredit(amount, ENDLESS_CASH_PICKUP);
	else
		this_player->cash += amount;
}

bool power_up_weapon(Player *this_player, uint port)
{
	const bool can_power_up = this_player->items.weapon[port].id != 0 &&  // not None
	                          this_player->items.weapon[port].power < 11; // not at max power
	if (can_power_up)
	{
		++this_player->items.weapon[port].power;
		shotMultiPos[port] = 0; // TODO: should be part of Player structure

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
