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
#include "extra_ships.h"
#include "game_state.h"
#include "helptext.h"
#include "hud.h"
#include "ingame_menu.h"
#include "lvlmast.h"
#include "mtrand.h"
#include "network.h"
#include "ship_skin.h"
#include "sprite.h"

#include <stdio.h>
#include <string.h>

Player player[2];

JE_word shipGr, shipGr2;
Sprite2_array *shipGrPtr, *shipGr2ptr;

uint gameplay_local_player_index(void)
{
	// Every dual-ship session gives each machine a ship of its own to read: both co-op modes and
	// Separate arcade. The linked arcade pair shares one HUD sidebar, so it stays on ship one.
	return dual_ship_mode() && isNetworkGame && thisPlayerNum >= 1 && thisPlayerNum <= 2
	     ? thisPlayerNum - 1 : 0;
}

/* Per-ship Super Arcade mode, clamped because it travels through saves and the wire. */
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

/* Separate arcade may scale rear guns; the linked pair aliases player two's lives there. */
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

	// Defer repainting to the rollback-safe tick poll in game_loop.c.
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
/* Shared credit still records the local wallet's income source in the run ledger. */
		for (uint i = 0; i < COUNTOF(player); ++i)
		{
			if (endlessMode && i == endlessEconomyIndex())
				endlessCashCredit(amount, endless_source);
			else
				player_add_cash(&player[i], amount);
		}
		return;
	}

	// Attribute income to the ship whose wallet is local on this machine.
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

/* Endless run-persistent hull: the outpost Reinforce tier plus the Ablative Plating perk. */
static void endlessApplyHullBonus(uint p)
{
	if (!endlessFxActive())
		return;

	// Personal perks: the plating (or Glass Cannon drawback) on this hull is this ship's own.
	const uint fxSaved = endlessFxPlayer();
	endlessSetFxPlayer(p);
	int a = (int)player[p].armor + endlessPerkArmorBonus();
	endlessSetFxPlayer(fxSaved);
	if (endlessMode)
		a += endlessArmorBonus[p];
	player[p].armor = (a < 1) ? 1 : (a > 250 ? 250 : a);  // byte-safe, so no JE_byte armor path wraps
}

void JE_getShipInfo(void)
{
	JE_boolean extraShip, extraShip2;

	// Extra ship ids are not valid ships[] indices; JE_SGr selects their real sheet below.
	shipGrPtr = (player[0].items.ship <= SHIP_DRAGONWING && ships[player[0].items.ship].shipgraphic > 500)
	          ? &spriteSheetT2000 : &spriteSheet9;
	shipGr2ptr = &spriteSheet9;

	powerAdd  = powerSys[player[0].items.generator].power;
	for (uint i = 0; i < COUNTOF(player); ++i)
		player[i].generator_power_add = powerSys[player[i].items.generator].power;

	// Extra ships require a file known to both peers.
	for (uint i = 0; i < COUNTOF(player); ++i)
		if (player[i].items.ship > 90 && !extraAvailFor(i))
			player[i].items.ship = 1;

	extraShip = player[0].items.ship > 90;
	if (extraShip)
	{
		JE_byte base = (player[0].items.ship - 91) * 15;
		shipGr = JE_SGr(0, player[0].items.ship - 90, &shipGrPtr);
		player[0].armor = extraShipsFor(0)[base + 7];
	}
	else
	{
		// Only ids above 90 are extra ships. IDs 20 through 90 fall beyond ships[], so use
		// entry 0 if an edited ship or older save carries a stray id.
		const uint shipIdx = (player[0].items.ship <= SHIP_DRAGONWING) ? player[0].items.ship : 0;
		shipGr = ships[shipIdx].shipgraphic - (shipGrPtr == &spriteSheetT2000 ? 500 : 0);
		player[0].armor = ships[shipIdx].dmg;
	}

	endlessApplyHullBonus(0);

	extraShip2 = player[1].items.ship > 90;
	if (extraShip2)
	{
		JE_byte base2 = (player[1].items.ship - 91) * 15;
		shipGr2 = JE_SGr(1, player[1].items.ship - 90, &shipGr2ptr);
		player[1].armor = extraShipsFor(1)[base2 + 7];
	}
	else if (dual_ship_mode())
	{
		const uint shipIdx = (player[1].items.ship <= SHIP_DRAGONWING) ? player[1].items.ship : 0;
		shipGr2ptr = (ships[shipIdx].shipgraphic > 500) ? &spriteSheetT2000 : &spriteSheet9;
		shipGr2 = ships[shipIdx].shipgraphic - (shipGr2ptr == &spriteSheetT2000 ? 500 : 0);
		player[1].armor = ships[shipIdx].dmg;
	}
	else
	{
		shipGr2 = 0;
		player[1].armor = 10;
	}

	if (coopEndlessMode)
		endlessApplyHullBonus(1);

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		// Arcade lives raise the full-restoration ceiling above the one-life hull value.
		player[i].hull_armor = player[i].armor;
		player[i].initial_armor = arcade_armor_max(&player[i]);
		player[i].armor = player[i].initial_armor;

		// ships[] stops at SHIP_DRAGONWING while an "extra" ship is only id > 90, so an id in
		// between would read past the end here too (see the shipGr fallback above).
		const uint shipIdx = (player[i].items.ship <= SHIP_DRAGONWING) ? player[i].items.ship : 0;
		uint temp = ((i == 0 && extraShip) ||
		             (i == 1 && extraShip2)) ? 2 : ships[shipIdx].ani;

		if (temp == 0)
		{
			player[i].shot_hit_area_x = 12;
			player[i].shot_hit_area_y = 10;
		}
		else
		{
			player[i].shot_hit_area_x = 11;
			player[i].shot_hit_area_y = 14;
		}
	}
}

void JE_initPlayerData(void)
{
	/* JE: New Game Items/Data */
	extraShipReturnReset();
	shipSkinReset();

	player[0].items.ship = 1;                     // USP Talon
	player[0].items.weapon[FRONT_WEAPON].id = 1;  // Pulse Cannon
	player[0].items.weapon[REAR_WEAPON].id = 0;   // None
	player[0].items.shield = 4;                   // Gencore High Energy Shield
	player[0].items.generator = 2;                // Advanced MR-12
	for (uint i = 0; i < COUNTOF(player[0].items.sidekick); ++i)
		player[0].items.sidekick[i] = 0;          // None
	player[0].items.special = 0;                  // None
	// Which Super Arcade ruleset a ship flies now lives on the ship (player_sa_ship), because
	// online Super Arcade gives the two players different ones.
	player[0].items.super_arcade_mode = SA_NONE;

	player[0].last_items = player[0].items;

	player[1].items = player[0].items;
	player[1].items.weapon[REAR_WEAPON].id = 15;  // Vulcan Cannon
	player[1].items.sidekick_level = 101;         // 101, 102, 103
	player[1].items.sidekick_series = 0;          // None

	gameHasRepeated = false;
	onePlayerAction = false;
	superArcadeMode = SA_NONE;
	superTyrian = false;
	twoPlayerMode = false;
	coopCampaignMode = false;
	coopEndlessMode = false;
	arcadeSeparateMode = false;
	timedBattleMode = false;
	endlessMode = false;

	secretHint = (mt_rand() % 3) + 1;

	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		for (uint i = 0; i < COUNTOF(player->items.weapon); ++i)
		{
			player[p].items.weapon[i].power = 1;
		}

		player[p].weapon_mode = 1;
		player[p].armor = player[p].hull_armor = ships[player[p].items.ship].dmg;

		player[p].is_dragonwing = (p == 1);
		player[p].lives = &player[p].items.weapon[player_lives_port(p)].power;

	}

	mainLevel = FIRST_LEVEL;
	saveLevel = FIRST_LEVEL;

	strcpy(lastLevelName, miscText[19]);
}

// Per-slot lookup: an unset nickname must fall back to "Player N" for that slot, never to the
// other side's nickname, or the named player's nick shows on both slots for the unnamed player.
const char *JE_getName(JE_byte pnum)
{
	if (pnum == thisPlayerNum)
	{
		if (network_player_name[0] != '\0')
			return network_player_name;
	}
	else if (network_opponent_name[0] != '\0')
	{
		return network_opponent_name;
	}

	return miscText[47 + pnum];
}

// Replace a leading "Player N" score label with that player's nickname.
void JE_playerScoreLabel(JE_byte pnum, char *out, size_t outSize)
{
	const char *const label = miscText[39 + pnum];   // "Player N Score:"
	const char *const who   = miscText[47 + pnum];   // "Player N"
	const size_t who_len = strlen(who);

	if (isNetworkGame && strncmp(label, who, who_len) == 0)
		snprintf(out, outSize, "%s%s", JE_getName(pnum), label + who_len);
	else
		snprintf(out, outSize, "%s", label);
}
