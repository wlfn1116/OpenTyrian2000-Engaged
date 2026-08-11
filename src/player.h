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
#ifndef PLAYER_H
#define PLAYER_H

#include "config.h"
#include "opentyr.h"

enum
{
	FRONT_WEAPON = 0,
	REAR_WEAPON = 1
};

enum
{
	LEFT_SIDEKICK = 0,
	RIGHT_SIDEKICK = 1
};

// Horizontal offset each way when BOTH sidekick slots hold a front-mounted (tr==2) option,
// so the two pods sit side by side instead of overlapping. Shared by gameplay (mainint.c)
// and the shop weapon preview (game_menu.c) so the two stay in sync.
#define FRONT_OPTION_SPREAD 10

// Large trailing companions draw below their stored position. Offset gameplay and preview shots
// to the body's center; see doc/notes.md#rendering.
#define SIDEKICK_TRAIL_SHOT_Y 7

typedef struct
{
	Uint8 ship;
	Uint8 generator;
	Uint8 shield;
	struct
	{
		Uint8 id;
		Uint8 power;
	} weapon[2];
	Uint8 sidekick[2];
	Uint8 special;
	
	// Dragonwing only:
	// repeatedly collecting the same powerup gives a series of sidekick upgrades
	Uint8 sidekick_series;
	Uint8 sidekick_level;
	
	// Single-player only
	Uint8 super_arcade_mode;  // stored with items for save compatibility
}
PlayerItems;

typedef struct
{
	// Fixed-width, not `ulong`: player[] is registered rollback state and netplay desync
	// recovery ships the snapshot to the peer, so an `unsigned long` here made the struct
	// 8 bytes wider per player on the consoles than on Windows.
	Uint32 cash;
	
	PlayerItems items, last_items;
	
	bool is_dragonwing;  // i.e., is player 2
	Uint8 *lives;
	
	// calculatable
	uint shield_max;
	uint initial_armor;
	uint hull_armor;  // the ship's own armour, before the arcade lives scaling raises the ceiling
	uint shot_hit_area_x, shot_hit_area_y;
	
	// state
	bool is_alive;
	uint invulnerable_ticks;  // ticks until ship can be damaged
	uint exploding_ticks;     // ticks until ship done exploding
	uint shield;
	uint armor;
	uint weapon_mode;
	uint superbombs;
	uint purple_balls_needed;
	
	// Online Campaign gives each ship the single-player generator and firing state. The original
	// globals remain the active scratch context so the weapon code itself stays unchanged.
	Uint16 generator_power;
	Uint16 generator_power_add;
	Uint8 shield_wait;
	Uint8 shot_repeat[11];
	Uint8 shot_multi_pos[11];
	bool port_config_change;
	bool port_config_done;
	float option_satellite_rotate;
	Sint32 option_attachment_move[2];
	bool option_attachment_linked[2];
	bool option_attachment_return[2];
	bool special_fire_held;
	Uint8 zinglon_duration;
	Uint8 astral_duration;
	Uint16 flare_duration;
	bool flare_start;
	Sint8 flare_color_change;
	Uint8 special_wait;
	Uint8 next_special_wait;
	bool spray_special;
	Sint8 special_weapon_filter;
	Sint8 special_weapon_freq;
	Uint16 special_weapon_wpn;
	bool special_link_to_player;

	int x, y;
	int old_x[20], old_y[20];
	
	int x_velocity, y_velocity;
	uint x_friction_ticks, y_friction_ticks;  // ticks until friction is applied
	
	int delta_x_shot_move, delta_y_shot_move;
	
	int last_x_shot_move, last_y_shot_move;
	int last_x_explosion_follow, last_y_explosion_follow;
	
	struct
	{
		// calculatable
		int ammo_max;
		uint ammo_refill_ticks_max;
		uint style;  // affects movement and size
		
		// state
		int x, y;
		int ammo;
		uint ammo_refill_ticks;
		
		bool animation_enabled;
		uint animation_frame;
		
		uint charge;
		uint charge_ticks;
	}
	sidekick[2];
}
Player;

extern Player player[2];

uint gameplay_local_player_index(void);
// The weapon bay whose power byte is player p's arcade life counter; bind player[].lives with it.
uint player_lives_port(uint p);
// The Super Arcade ship this ship flies (1..SA) or SA_NONE; the two can differ online.
uint player_sa_ship(const Player *);
// The front gun a Super Arcade colour ball hands this ship: the ball carries a slot, and the
// slot is read out of the COLLECTOR's own arsenal, so one colour pays two ships differently.
uint player_sa_ball_weapon(const Player *, uint slot);

// Rounds per segment on the sidekick ammo gauge, sized so a full magazine is at most ten
// segments and stays inside the 29px HUD strip. Rounding UP matters now that the endless
// Ordnance Reserves perk produces magazines that aren't round numbers (a 26-round magazine
// at the old `ammo_max / 10` would have drawn 13 segments, running off the end of the bar).
#define AMMO_GAUGE_STEP(ammo_max) ((uint)MAX(1, ((ammo_max) + 9) / 10))

static inline bool all_players_dead(void)
{
	return (!player[0].is_alive && (!twoPlayerMode || !player[1].is_alive));
}

static inline bool all_players_alive(void)
{
	return (player[0].is_alive && (!twoPlayerMode || player[1].is_alive));
}

void calc_purple_balls_needed(Player *);
// Cash off the playfield. Routes player 1's share through the endless ledger; use it for every
// pickup credit so the run-over earnings breakdown stays accurate.
void player_award_pickup_cash(Player *, long amount);
// Cash off a destroyed enemy, credited to the player whose shot killed it.
void player_award_kill_cash(Player *, long amount);
// An elite or champion bounty: the kill rules, booked under the ledger's own bounty row.
void player_award_bounty_cash(Player *, long amount);

/* Online Campaign credit sharing.
 * coopSharedCredit is the host's stored preference; the session value arrives in the connect
 * packet's settings block, so both machines award identical cash. Shared pays every kill and
 * score pickup to both players at full value; Individual pays the shot's owner or the collector. */
extern bool coopSharedCredit;
void coop_set_session_shared_credit(bool shared);
bool coop_credit_is_shared(void);

/* Individual credit splits between two wallets what one player would have earned alone. Double
 * Pickups pays every cash and gem pickup twice over to make up part of that; it is meaningless
 * under Shared, where both already collect in full, so the row only shows under Individual. */
// Online Arcade lobby preference: fly the classic linked pair, or two Separate personal
// arcades. The session flag it arms is arcadeSeparateMode (config.h).
extern bool arcadeSeparateShips;

// Double Earnings: under Individual credit, combat income (pickups, kills, bounties) pays
// twice to compensate the split take. Shared credit stands it down.
extern bool coopDoubleEarnings;
void coop_set_session_double_earnings(bool on);
bool coop_earnings_are_doubled(void);

bool power_up_weapon(Player *, uint port);
void handle_got_purple_ball(Player *);

// Arcade lives scale armour and shields from the hull values to a full bar.
#define ARCADE_LIVES_MAX 11  // the cap JE_eventSystem/mainint enforce on *player[].lives
#define ARCADE_FULL_BAR  28  // both HUD gauges top out here (strongest shield is mpwr 14 -> 28)

bool arcade_life_scaling_active(void);
bool arcade_rear_scale_active(void);     // 1P arcade only: lives raise the rear gun above its pickups
uint arcade_weapon_power(const Player *, uint port);  // the level a bay fires at, 1-11
uint arcade_armor_max(const Player *);   // == hull_armor outside the arcade modes
uint arcade_shield_max(const Player *);  // == shields[].mpwr * 2 outside the arcade modes
void arcade_rescale_to_lives(Player *);  // re-derive both ceilings after a life is gained or lost

void coop_ship_runtime_reset(void);

#endif // PLAYER_H
