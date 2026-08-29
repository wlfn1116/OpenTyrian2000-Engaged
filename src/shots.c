/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2013  The OpenTyrian Development Team
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
#include "shots.h"

#include "config.h"
#include "custom_weapon.h"
#include "endless.h"
#include "mainint.h"
#include "net_style.h"
#include "player.h"
#include "render_list.h"
#include "sim_math.h"
#include "sprite.h"
#include "video.h"
#include "varz.h"

// The extra entry preserves the original allocation size.
PlayerShotDataType playerShotData[MAX_PWEAPON + 1]; /* [1..MaxPWeapon+1] */
JE_byte shotAvail[MAX_PWEAPON]; /* [1..MaxPWeapon] */   /*0:Avail 1-255:Duration left*/

/* Guidance Package targets the screen position of a live, shootable hull. Stock homing uses map x
 * unless Guided Aim is enabled. */
static bool shot_guidance_target_ok(int slot)
{
	return enemyAvail[slot] == 0 && !enemy[slot].scoreitem
	    && enemy[slot].armorleft > 0 && enemy[slot].armorleft < 255;
}

// Nearest steerable enemy to (x, y), 1-based like aimAtEnemy; 0 when the field holds none.
static JE_byte shot_guidance_nearest(int x, int y)
{
	int best_dist = 65000;
	JE_byte closest = 0;
	for (int slot = 0; slot < (int)COUNTOF(enemy); ++slot)
	{
		if (!shot_guidance_target_ok(slot))
			continue;
		const int dist = abs(enemy[slot].ex + enemy[slot].mapoffset - x) + abs(enemy[slot].ey - y);
		if (dist < best_dist)
		{
			best_dist = dist;
			closest = (JE_byte)(slot + 1);
		}
	}
	return closest;
}

// Only a pattern pinned to the ship on both axes (a shield ring) is left alone: steered off the
// ship it would guard nothing.
static bool shot_guidance_can_steer(const PlayerShotDataType *shot)
{
	return !(shot->shotComplicated
	         && shot->shotXM >= SHOT_ATTACHED_VEL_MIN && shot->shotYM >= SHOT_ATTACHED_VEL_MIN);
}

/* One velocity nudge on one axis. A ship-relative velocity (see SHOT_ATTACHED_VEL_MIN) is
 * nudged within its range, so the shot keeps riding the ship and its curve travels with it; a
 * free velocity is kept out of that range. */
static void shot_guidance_nudge(JE_integer *vel, bool positive, bool xAxis)
{
	// The both-axes pin is not part of the x band.
	if (xAxis && *vel == SHOT_ATTACHED_VEL_MIN)
		return;

	const int lowest = xAxis ? SHOT_ATTACHED_VEL_MIN + 1 : SHOT_ATTACHED_VEL_MIN;
	int v = *vel + (positive ? 1 : -1);
	if (*vel >= SHOT_ATTACHED_VEL_MIN)
		v = v < lowest ? lowest : (v > SHOT_ATTACHED_VEL_MAX ? SHOT_ATTACHED_VEL_MAX : v);
	else if (v >= SHOT_ATTACHED_VEL_MIN)
		v = SHOT_ATTACHED_VEL_MIN - 1;
	*vel = (JE_integer)v;
}

void player_shot_aim_step(PlayerShotDataType *shot)
{
	const bool guidance = (shot->aimDelayMax & SHOT_AIM_GUIDANCE) != 0;
	shot->aimDelay = shot->aimDelayMax & SHOT_AIM_DELAY_MASK;

	// A steered shot whose enemy is gone looks for the next one; with none left it flies straight.
	if (guidance && (shot->aimAtEnemy == 0 || !shot_guidance_target_ok(shot->aimAtEnemy - 1)))
		shot->aimAtEnemy = shot_guidance_nearest(shot->shotX, shot->shotY);
	if (shot->aimAtEnemy == 0)
		return;

	const struct JE_SingleEnemyType *target = &enemy[shot->aimAtEnemy - 1];
	if (guidance)
	{
		shot_guidance_nudge(&shot->shotXM, shot->shotX < target->ex + target->mapoffset, true);
		shot_guidance_nudge(&shot->shotYM, shot->shotY < target->ey, false);
	}
	else if (enemyAvail[shot->aimAtEnemy - 1] != 1)
	{
		const int target_x = target->ex + (guidedShotScreenAim ? target->mapoffset : 0);
		if (shot->shotX < target_x)
			shot->shotXM++;
		else
			shot->shotXM--;

		if (shot->shotY < target->ey)
			shot->shotYM++;
		else
			shot->shotYM--;
	}
	else
	{
		// Stock rule for a weapon-table shot whose enemy died: veer off sideways.
		if (shot->shotXM > 0)
			shot->shotXM++;
		else
			shot->shotXM--;
	}
}

// Shots inherit their owner's opacity but never its dye.
static NetShipStyle shot_draw_style(JE_byte playerNum)
{
	return netStyleForShot(playerNum >= 1 ? (uint)playerNum - 1u : 0u);
}

// Preserve special-shot blending at half of the selected opacity.
static void draw_player_shot_special(int x, int y, unsigned int frame, NetShipStyle style)
{
	if (style.opacity >= NET_STYLE_SOLID)
		blit_sprite_blend(VGAScreen, x, y, OPTION_SHAPES, frame);
	else
		blit_sprite_alpha(VGAScreen, x, y, OPTION_SHAPES, frame, -1, (Uint8)(style.opacity / 2));
}

// Draw one shot with its returned-shot tint and owner opacity. Faded shots omit shadows.
static void draw_player_shot_sprite(int x, int y, JE_word sprite_frame, Uint8 tint,
                                    NetShipStyle style, bool shadow)
{
	Sprite2_array *const sheet = (sprite_frame > 500) ? &spriteSheet12 : &spriteSheet8;
	const unsigned int frame = (sprite_frame > 500) ? sprite_frame - 500 : sprite_frame;

	if (shadow && style.opacity >= NET_STYLE_SOLID)
		blit_sprite2_darken(VGAScreen, x, y + shadowYDist, *sheet, frame);

	if (style.opacity < NET_STYLE_SOLID)
		blit_sprite2_alpha(VGAScreen, x, y, *sheet, frame,
		                   tint != 0 ? (int)(tint >> 4) : -1, style.opacity);
	else if (tint != 0)
		blit_sprite2_filter_bright(VGAScreen, x, y, *sheet, frame, tint | ENDLESS_SHOT_BRIGHT);
	else
		blit_sprite2(VGAScreen, x, y, *sheet, frame);
}

void simulate_player_shots(void)
{
	/* Player Shot Images */
	for (int z = 0; z < MAX_PWEAPON; z++)
	{
		if (shotAvail[z] != 0)
		{
			shotAvail[z]--;
			if (z != MAX_PWEAPON - 1)
			{
				PlayerShotDataType* shot = &playerShotData[z];

				// Entry position: the tick's delta becomes the render-list extrapolation
				// velocity (as in player_shot_move_and_draw), for the smooth shop preview.
				const int rl_shot_old_x = shot->shotX, rl_shot_old_y = shot->shotY;

				shot->shotXM += shot->shotXC;

				if (shot->shotXM <= 100)
					shot->shotX += shot->shotXM;

				shot->shotYM += shot->shotYC;
				shot->shotY += shot->shotYM;

				if (shot->shotYM > 100)
				{
					shot->shotY -= 120;
					shot->shotY += player[0].delta_y_shot_move;
				}

				if (shot->shotComplicated != 0)
				{
					shot->shotDevX += shot->shotDirX;
					shot->shotX += shot->shotDevX;

					if (abs(shot->shotDevX) == shot->shotCirSizeX)
						shot->shotDirX = -shot->shotDirX;

					shot->shotDevY += shot->shotDirY;
					shot->shotY += shot->shotDevY;

					if (abs(shot->shotDevY) == shot->shotCirSizeY)
						shot->shotDirY = -shot->shotDirY;
					/*Double Speed Circle Shots - add a second copy of above loop*/
				}

				int tempShotX = shot->shotX;
				int tempShotY = shot->shotY;

				if (shot->shotX < 0 || shot->shotX > 140 ||
				    shot->shotY < 0 || shot->shotY > 170)
				{
					shotAvail[z] = 0;
					goto draw_player_shot_loop_end;
				}

				JE_word anim_frame = shot->shotGr + shot->shotAni;
				if (++shot->shotAni == shot->shotAniMax)
					shot->shotAni = 0;

				if (anim_frame < 60000)
				{
					rl_current_id = RL_ID_PSHOT_BASE + z;
					rl_current_vel_x = tempShotX - rl_shot_old_x;
					rl_current_vel_y = tempShotY - rl_shot_old_y;
					rl_current_acc_x = shot->shotXC;
					rl_current_acc_y = shot->shotYC;
					if (anim_frame > 1000)
					{
						// Match the in-game shot draw (player_shot_move_and_draw): a shot graphic > 1000 leaves a
						// superspark trail (colour bank = the thousands digit; see JE_doSP).
						JE_doSP(tempShotX+1 + 6, tempShotY + 6, 5, 3, (anim_frame / 1000) << 4,
						        superSparkCapForSprite(shot->shotGr % 1000));
						anim_frame = anim_frame % 1000;
					}
					draw_player_shot_sprite(tempShotX + 1, tempShotY, anim_frame, shot->tint,
					                        shot_draw_style(shot->playerNumber), false);
					rl_current_id = 0;
					rl_current_vel_x = 0;
					rl_current_vel_y = 0;
					rl_current_acc_x = 0;
					rl_current_acc_y = 0;
				}
				else if (anim_frame > 60000)
				{
					// Blended "special" sprite (e.g. the Plasma Storm cloud tiles). Mirror
					// player_shot_move_and_draw so the weapon-simulator preview (custom
					// weapon creator) shows these looks instead of silently skipping them.
					rl_current_id = RL_ID_PSHOT_BASE + z;
					rl_current_vel_x = tempShotX - rl_shot_old_x;
					rl_current_vel_y = tempShotY - rl_shot_old_y;
					rl_current_acc_x = shot->shotXC;
					rl_current_acc_y = shot->shotYC;
					draw_player_shot_special(tempShotX + 1, tempShotY, anim_frame - 60001,
					                         shot_draw_style(shot->playerNumber));
					rl_current_id = 0;
					rl_current_vel_x = 0;
					rl_current_vel_y = 0;
					rl_current_acc_x = 0;
					rl_current_acc_y = 0;
				}
			}

draw_player_shot_loop_end:
			;
		}
	}
}

// Endless Opening Salvo: the spark cue marking a boosted shot, coloured from the shot's own sprite
// so each weapon flashes its own hue. Kept sparse and short-lived so a wide many-shot weapon does
// not fill the screen with it; see doc/notes.md#gauges-and-effects.
#define SALVO_LAUNCH_SPARKS     6  // one-off puff on the shot's first drawn tick
#define SALVO_LAUNCH_REACH      5
#define SALVO_LAUNCH_LIFE_TICKS 4
#define SALVO_TRAIL_SPARKS      2  // per tick in flight
#define SALVO_TRAIL_REACH       3
#define SALVO_TRAIL_LIFE_TICKS  3
#define SALVO_SPARK_BRIGHT      5  // shade lift for the shortened life; see JE_doSPBrief

// (cx,cy) is the shot centre; `launch` picks the puff over the flight wisp.
static void salvo_sparks_at(int cx, int cy, Uint8 bank, bool launch)
{
	JE_doSPBrief(cx, cy, launch ? SALVO_LAUNCH_SPARKS : SALVO_TRAIL_SPARKS,
	             launch ? SALVO_LAUNCH_REACH : SALVO_TRAIL_REACH, bank << 4,
	             launch ? SALVO_LAUNCH_LIFE_TICKS : SALVO_TRAIL_LIFE_TICKS, SALVO_SPARK_BRIGHT);
}

// ...for a shot drawn from the packed sprite SHEETS (`sprite_frame` with the >1000 tag stripped).
static void salvo_shot_sparks(int x, int y, JE_word sprite_frame, bool launch)
{
	const Uint8 bank = (sprite_frame > 500)
		? sprite2_dominant_bank(spriteSheet12, sprite_frame - 500)
		: sprite2_dominant_bank(spriteSheet8, sprite_frame);

	salvo_sparks_at(x + 6, y + 6, bank, launch);
}

static const JE_word linkMultiGr[17] /* [0..16] */ =
	{77,221,183,301,1,282,164,202,58,201,163,281,39,300,182,220,77};
static const JE_word linkSonicGr[17] /* [0..16] */ =
	{85,242,131,303,47,284,150,223,66,224,149,283,9,302,130,243,85};
static const JE_word linkMult2Gr[17] /* [0..16] */ =
	{78,299,295,297,2,278,276,280,59,279,275,277,40,296,294,298,78};

void player_shot_set_direction(JE_integer shot_id, uint weapon_id, JE_real direction)
{
	PlayerShotDataType* shot = &playerShotData[shot_id];

	shot->shotXM = -roundf(sim_sinf(direction) * shot->shotYM);
	shot->shotYM = -roundf(sim_cosf(direction) * shot->shotYM);

	// Some weapons have sprites for each direction, use those.
	int rounded_dir;

	switch (weapon_id)
	{
	case 27:
	case 32:
	case 10:
		rounded_dir = roundf(direction * (16 / (2 * M_PI)));  /*16 directions*/
		shot->shotGr = linkMultiGr[rounded_dir];
		break;
	case 28:
	case 33:
	case 11:
		rounded_dir = roundf(direction * (16 / (2 * M_PI)));  /*16 directions*/
		shot->shotGr = linkSonicGr[rounded_dir];
		break;
	case 30:
	case 35:
	case 14:
		if (direction > M_PI_2 && direction < M_PI + M_PI_2)
		{
			shot->shotYC = 1;
		}
		break;
	case 38:
	case 22:
		rounded_dir = roundf(direction * (16 / (2 * M_PI)));  /*16 directions*/
		shot->shotGr = linkMult2Gr[rounded_dir];
		break;
	}
}

void player_shot_hit_offset(JE_word sprite_frame, int *out_dx, int *out_dy)
{
	if (sprite_frame > 60000)  // special weapon: drawn from the sprite table, which stores its size
	{
		const unsigned int id = sprite_frame - 60001;
		// A special is already taken from the middle of its sprite in both modes; only the pixel
		// its blit is offset by is new, so Classic keeps the point the vanilla test used.
		*out_dx = (centeredShotHitboxes ? 1 : 0) + sprite(OPTION_SHAPES, id)->width / 2;
		*out_dy = sprite(OPTION_SHAPES, id)->height / 2;
		return;
	}

	*out_dx = 0;
	*out_dy = 0;

	if (!centeredShotHitboxes)
		return;

	if (sprite_frame > 1000)  // superspark trail marker; the frame itself is what is left below it
		sprite_frame %= 1000;

	if (sprite_frame > 500)
		sprite2_center_offset(spriteSheet12, sprite_frame - 500, out_dx, out_dy);
	else
		sprite2_center_offset(spriteSheet8, sprite_frame, out_dx, out_dy);

	*out_dx += 1;  // the shot is blitted one pixel right of the position it is stored at
}

void enemy_shot_hit_offset(JE_word sgr, JE_word animate, int *out_dx, int *out_dy)
{
	*out_dx = 0;
	*out_dy = 0;

	if (!centeredShotHitboxes)
		return;

	// The sheet is chosen by the base graphic, not by the animated frame; the draw in tyrian2.c
	// splits it the same way, and a shot whose animation crosses 500 must not change sheets.
	if (sgr >= 500)
		sprite2_center_offset(spriteSheet12, sgr + animate - 500, out_dx, out_dy);
	else
		sprite2_center_offset(spriteSheet8, sgr + animate, out_dx, out_dy);
}

bool player_shot_move_and_draw(
		int shot_id, bool* out_is_special,
		int* out_shotx, int* out_shoty,
		JE_integer* out_shot_damage, JE_byte* out_blast_filter,
		JE_byte* out_chain, JE_byte* out_playerNum,
		JE_word* out_special_radiusw, JE_word* out_special_radiush,
		int* out_hit_dx, int* out_hit_dy)
{
	PlayerShotDataType* shot = &playerShotData[shot_id];

	*out_hit_dx = 0;
	*out_hit_dy = 0;

	shotAvail[shot_id]--;
	if (shot_id != MAX_PWEAPON - 1)
	{
		// Entry position: the tick's delta becomes the render-list extrapolation
		// velocity (rl_current_vel_*, set at the blit below).
		const int rl_shot_old_x = shot->shotX, rl_shot_old_y = shot->shotY;

		shot->shotXM += shot->shotXC;
		shot->shotX += shot->shotXM;
		JE_integer tmp_shotXM = shot->shotXM;

		if (shot->shotXM > 100)
		{
			if (shot->shotXM == 101)
			{
				shot->shotX -= 101;
				shot->shotX += player[shot->playerNumber-1].delta_x_shot_move;
				shot->shotY += player[shot->playerNumber-1].delta_y_shot_move;
			}
			else
			{
				shot->shotX -= 120;
				shot->shotX += player[shot->playerNumber-1].delta_x_shot_move;
			}
		}

		shot->shotYM += shot->shotYC;
		shot->shotY += shot->shotYM;

		if (shot->shotYM > 100)
		{
			shot->shotY -= 120;
			shot->shotY += player[shot->playerNumber-1].delta_y_shot_move;
		}

		if (shot->shotComplicated != 0)
		{
			shot->shotDevX += shot->shotDirX;
			shot->shotX += shot->shotDevX;

			if (abs(shot->shotDevX) == shot->shotCirSizeX)
				shot->shotDirX = -shot->shotDirX;

			shot->shotDevY += shot->shotDirY;
			shot->shotY += shot->shotDevY;

			if (abs(shot->shotDevY) == shot->shotCirSizeY)
				shot->shotDirY = -shot->shotDirY;

			/*Double Speed Circle Shots - add a second copy of above loop*/
		}

		*out_shotx = shot->shotX;
		*out_shoty = shot->shotY;

		// Wide cull margins keep interpolated exits and returning projectile arcs visible.
		// Decelerating shots are culled above -15 once they stop ascending; a riding
		// velocity ascends by its drift from the resting value.
		const int ym_drift = shot->shotYM >= SHOT_ATTACHED_VEL_MIN
		                     ? shot->shotYM - SHOT_ATTACHED_VEL_REST : shot->shotYM;
		if (shot->shotX < -34 || shot->shotX > PLAYFIELD_WIDTH + 34 ||
			shot->shotY < -40 || shot->shotY > 240 ||
			(shot->shotY < -15 && ym_drift >= 0))
		{
			shotAvail[shot_id] = 0;
			return false;
		}

		if (shot->shotTrail != 255)
		{
			if (shot->shotTrail == 98 || shot->shotTrail == 198)
				JE_setupExplosion(shot->shotX - shot->shotXM, shot->shotY - shot->shotYM, 0, shot->shotTrail, false, false);
			else
				JE_setupExplosion(shot->shotX, shot->shotY, 0, shot->shotTrail, false, false);
		}

		// A Guidance Package shot keeps polling for an enemy even while it has none to chase.
		if (shot->aimAtEnemy != 0 || (shot->aimDelayMax & SHOT_AIM_GUIDANCE))
		{
			if (--shot->aimDelay == 0)
				player_shot_aim_step(shot);
		}

		JE_word sprite_frame = shot->shotGr + shot->shotAni;
		if (++shot->shotAni == shot->shotAniMax)
			shot->shotAni = 0;

		// Taken from the frame about to be drawn, before the encodings below are stripped off it.
		player_shot_hit_offset(sprite_frame, out_hit_dx, out_hit_dy);

		*out_shot_damage = shot->shotDmg;
		*out_blast_filter = shot->shotBlastFilter;
		*out_chain = shot->chainReaction;
		*out_playerNum = shot->playerNumber;

		*out_is_special = sprite_frame > 60000;

		// Attach tracking-shot axes to the render-rate ship and extrapolate the moving
		// axis from this tick's final screen delta.
		rl_current_id = RL_ID_PSHOT_BASE + shot_id;
		// The linked-Dragonwing aim markers are recreated every tick, so their
		// pool slot can drift; a stable id keeps them paired across frames and
		// the aim swing interpolates instead of stepping at the tick rate.
		bool link_marker = false;
		for (int k = 0; k < 3; ++k)
		{
			if (link_marker_slot[k] == (int)shot_id)
			{
				rl_current_id = RL_ID_LINKGUN_BASE + k;
				link_marker = true;
				break;
			}
		}
		rl_current_vel_x = shot->shotX - rl_shot_old_x;
		rl_current_vel_y = shot->shotY - rl_shot_old_y;
		// Acceleration lets the travelling axis extrapolate a decelerating shot
		// (e.g. Vulcan Cannon) without overshooting and snapping back; the attached
		// axis ignores it (orbit math uses pure velocity).
		rl_current_acc_x = shot->shotXC;
		rl_current_acc_y = shot->shotYC;
		rl_shot_attach = (shot->shotXM > 100 ? 1 : 0)
		               | (shot->shotYM > 100 ? 2 : 0)
		               | ((shot->playerNumber - 1) << 2);
		// The aim markers orbit the fused ship: attach BOTH axes to the carrier
		// (player 1 = index 0), so they ride the render-rate ship instead of
		// interpolating a tick behind it; their own angular motion still
		// interpolates from the cross-frame pairing.
		if (link_marker)
			rl_shot_attach = 3;
		if (*out_is_special)
		{
			draw_player_shot_special(*out_shotx + 1, *out_shoty, sprite_frame - 60001,
			                         shot_draw_style(shot->playerNumber));

			*out_special_radiusw = sprite(OPTION_SHAPES, sprite_frame - 60001)->width / 2;
			*out_special_radiush = sprite(OPTION_SHAPES, sprite_frame - 60001)->height / 2;

			// Opening Salvo: blended shots come from the sprite TABLE, so they need the table
			// colour walker, and centre on the real sprite rather than a bullet's +6/+6.
			if (shot->salvoBoost)
			{
				salvo_sparks_at(*out_shotx + 1 + *out_special_radiusw, *out_shoty + *out_special_radiush,
				                sprite_dominant_bank(OPTION_SHAPES, sprite_frame - 60001),
				                shot->salvoBoost == 1);
				shot->salvoBoost = 2;
			}
		}
		else
		{
			// Weapons already tagged with a superspark trail keep their native plume as it is; an
			// Opening Salvo adds only the launch puff on top.
			const bool ownTrail = sprite_frame > 1000;
			if (ownTrail)
			{
				JE_doSP(*out_shotx+1 + 6, *out_shoty + 6, 5, 3, (sprite_frame / 1000) << 4,
				        superSparkCapForSprite(shot->shotGr % 1000));
				sprite_frame = sprite_frame % 1000;
			}
			// salvoBoost 1 = first drawn tick (always the launch puff), stepping to 2 for the
			// flight wisp, which a weapon with its own plume does not need. Both stay truthy,
			// which is all the collision-time damage bonus tests.
			if (shot->salvoBoost)
			{
				if (shot->salvoBoost == 1)
					salvo_shot_sparks(*out_shotx + 1, *out_shoty, sprite_frame, true);
				else if (!ownTrail)
					salvo_shot_sparks(*out_shotx + 1, *out_shoty, sprite_frame, false);
				shot->salvoBoost = 2;
			}
			// A tinted shot (a returned elite or champion bullet) is drawn as the enemy loop drew it.
			const bool shadow = background2 && *out_shoty + shadowYDist < 190 && tmp_shotXM < 100;
			draw_player_shot_sprite(*out_shotx + 1, *out_shoty, sprite_frame, shot->tint,
			                        shot_draw_style(shot->playerNumber), shadow);
		}
		rl_current_id = 0;
		rl_current_vel_x = 0;
		rl_current_vel_y = 0;
		rl_current_acc_x = 0;
		rl_current_acc_y = 0;
		rl_shot_attach = 0;
	}

	return true;
}

// Opening Salvo tag for the shots created by the next player_shot_create call, replacing the live
// window test. Only player_shot_create_chained sets these, and it clears them before returning, so
// they never span a sim tick and need no rollback entry.
static bool salvoBoostOverride = false;    // take the tag from salvoBoostFromParent
static bool salvoBoostFromParent = false;  // the chain parent's tag, valid while the override is set

// Offset from a circlesize shot's spawn point to the centre of the loop it walks, in pixels on one
// axis. shotDev steps as a triangle wave and the position is its running sum, so the loop closes
// after 4 * cir_size ticks and one period covers all of it.
static int shot_circle_center_offset_px(int dev, int dir, int cir_size)
{
	if (cir_size <= 0)
		return 0;

	int pos = 0, lowest = 0, highest = 0;
	for (int tick = 0; tick < 4 * cir_size; ++tick)
	{
		dev += dir;
		pos += dev;
		if (abs(dev) == cir_size)
			dir = -dir;

		if (pos < lowest)
			lowest = pos;
		if (pos > highest)
			highest = pos;
	}

	return (lowest + highest) / 2;
}

JE_integer player_shot_create(JE_word portNum, uint bay_i, JE_word PX, JE_word PY, JE_word mouseX, JE_word mouseY, JE_word wpNum, JE_byte playerNum)
{
	// The free-power and gun-jam rules below belong to whichever ship is firing.
	endlessSetFxPlayer(playerNum >= 1 ? (uint)playerNum - 1 : 0);

	static const JE_byte soundChannel[11] /* [1..11] */ = {0, 2, 4, 4, 2, 2, 5, 5, 1, 4, 1};

	// Bounds check
	if (portNum > PORT_NUM || wpNum <= 0 || wpNum > WEAP_NUM)
		return MAX_PWEAPON;

	const JE_WeaponType* weapon = &weapons[wpNum];

	uint power_use = weaponPort[portNum].poweruse;
	if (expertMode)
		power_use = power_use * expertEnergyPct / 100;

	// Endless "Efficient Coils" perk: trim the generator draw per main-weapon shot (100 = normal),
	// so a given generator sustains heavier fire. Scales the base cost before the special/boost zeroing.
	if (endlessFxActive())
		power_use = power_use * endlessPerkPowerUsePercent() / 100;

	// Special-weapon shots are paid for upfront in shield/armor; they must not also
	// drain the generator, or a sustained special (e.g. an active Minefield) starves
	// the main weapon of power.
	if (bay_i == SHOT_SPECIAL || bay_i == SHOT_SPECIAL2)
		power_use = 0;

	// Endless kill-fire BOON (Turbodrive / Overdrive): the boosted fire rate must not drain the
	// generator faster, so shots fired during the window are power-free. An evil curse fires SLOWER,
	// so it gets no such break; normal power cost applies.
	if (endlessFxActive() && endlessTurbodriveActive() && !endlessKillFireIsEvil())
		power_use = 0;

	// Endless Opening Salvo perk: a charged volley (the fire path arms it for the front gun only)
	// costs no generator power. Its shots are also tagged below for the collision-time damage bonus.
	if (endlessFxActive() && endlessOpeningSalvoVolleyActive())
		power_use = 0;

	if (!cheatInfiniteGenerator)
	{
		if (power < power_use)
			return MAX_PWEAPON;
		power -= power_use;
	}

	if (weapon->sound > 0)
		soundQueue[soundChannel[bay_i]] = weapon->sound;

	// The shot is paid for, so the gun has fired. Endless marks its record with a C when that gun is
	// either player's custom weapon; the custom sidekick fires through the same port, so one test
	// covers all bays.
	if (customWeaponPortIsCustom(portNum))
		endlessNoteCustomWeaponShot();

	// Endless Opening Salvo perk: tag the shots that belong to a charged volley, so the collision
	// applies the damage bonus only to those. A chain-reaction child takes its parent's tag and
	// ignores the window, which keeps the bonus with the volley that launched the carrier.
	const JE_byte salvo_tag = (salvoBoostOverride
	                           ? salvoBoostFromParent
	                           : (endlessFxActive() && endlessOpeningSalvoVolleyActive())) ? 1 : 0;

	int shot_id = MAX_PWEAPON;
	/*Rot*/
	for (int multi_i = 1; multi_i <= weapon->multi; multi_i++)
	{
		for (shot_id = 0; shot_id < MAX_PWEAPON; shot_id++)
			if (shotAvail[shot_id] == 0)
				break;
		if (shot_id == MAX_PWEAPON)
			return MAX_PWEAPON;

		// Fire-cursor wrap must test >=, not ==: the cursor is a persistent per-bay global, so a
		// weapon swap mid-cycle can leave it above the new max, silently killing the gun.
		if ((weapon->max != 0 && shotMultiPos[bay_i] >= weapon->max) ||
		    shotMultiPos[bay_i] >= WEAPON_MULTI_MAX ||
		    (weapon->max == 0 && shotMultiPos[bay_i] > 8))
			shotMultiPos[bay_i] = 1;
		else
			shotMultiPos[bay_i]++;

		PlayerShotDataType* shot = &playerShotData[shot_id];
		shot->chainReaction = 0;
		shot->salvoBoost = salvo_tag;
		// A recycled slot must not inherit the previous bullet's lockout, damage remainder or tint.
		shot->pierceLock = 0;
		shot->pierceLockCarry = 0;
		shot->pierceLockPending = 0;
		shot->pierceDmgCarry = 0;
		shot->tint = 0;

		shot->playerNumber = playerNum;

		shot->shotAni = 0;

		shot->shotComplicated = weapon->circlesize != 0;

		if (weapon->circlesize == 0)
		{
			shot->shotDevX = 0;
			shot->shotDirX = 0;
			shot->shotDevY = 0;
			shot->shotDirY = 0;
			shot->shotCirSizeX = 0;
			shot->shotCirSizeY = 0;
		}
		else
		{
			JE_byte circsize = weapon->circlesize;

			if (circsize > 19)
			{
				JE_byte circsize_mod20 = circsize % 20;
				shot->shotCirSizeX = circsize_mod20;
				shot->shotDevX = circsize_mod20 >> 1;

				circsize = circsize / 20;
				shot->shotCirSizeY = circsize;
				shot->shotDevY = circsize >> 1;
			}
			else
			{
				shot->shotCirSizeX = circsize;
				shot->shotCirSizeY = circsize;
				shot->shotDevX = circsize >> 1;
				shot->shotDevY = circsize >> 1;
			}
			shot->shotDirX = 1;
			shot->shotDirY = -1;
		}

		shot->shotTrail = weapon->trail;

		// For trail 198, only the Flying Punch center tile leaves a trail.
		if (shot->shotTrail == 198 && shotMultiPos[bay_i] > 1)
		{
			shot->shotTrail = 255;
		}

		if (weapon->attack[shotMultiPos[bay_i]-1] > 99 && weapon->attack[shotMultiPos[bay_i]-1] < 250)
		{
			shot->chainReaction = weapon->attack[shotMultiPos[bay_i]-1] - 100;
			shot->shotDmg = 1;
		}
		else
		{
			shot->shotDmg = weapon->attack[shotMultiPos[bay_i]-1];
		}

		shot->shotBlastFilter = weapon->shipblastfilter;

		JE_integer tmp_by = weapon->by[shotMultiPos[bay_i]-1];

		/*Note: Only front selection used for player shots...*/

		shot->shotX = PX + weapon->bx[shotMultiPos[bay_i]-1];

		shot->shotY = PY + tmp_by;
		shot->shotYC = -weapon->acceleration;
		shot->shotXC = weapon->accelerationx;

		shot->shotXM = weapon->sx[shotMultiPos[bay_i]-1];

		// Not sure what this field does exactly.
		JE_byte del = weapon->del[shotMultiPos[bay_i]-1];

		if (del == 121)
		{
			shot->shotTrail = 0;
			del = 255;
		}

		shot->shotGr = weapon->sg[shotMultiPos[bay_i]-1];
		if (shot->shotGr == 0)
			shotAvail[shot_id] = 0;
		else
			shotAvail[shot_id] = del;

		if (del > 100 && del < 120)
			shot->shotAniMax = (del - 100 + 1);
		else
			shot->shotAniMax = weapon->weapani + 1;

		if (del == 99 || del == 98)
		{
			tmp_by = PX - mouseX;
			if (tmp_by < -5)
				tmp_by = -5;
			else if (tmp_by > 5)
				tmp_by = 5;
			shot->shotXM += tmp_by;
		}

		if (del == 99 || del == 100)
		{
			tmp_by = PY - mouseY - weapon->sy[shotMultiPos[bay_i]-1];
			if (tmp_by < -4)
				tmp_by = -4;
			else if (tmp_by > 4)
				tmp_by = 4;
			shot->shotYM = tmp_by;
		}
		else if (weapon->sy[shotMultiPos[bay_i]-1] == 98)
		{
			shot->shotYM = 0;
			shot->shotYC = -1;
		}
		else if (weapon->sy[shotMultiPos[bay_i]-1] > 100)
		{
			shot->shotYM = weapon->sy[shotMultiPos[bay_i]-1];
			shot->shotY -= player[shot->playerNumber-1].delta_y_shot_move;
		}
		else
		{
			shot->shotYM = -weapon->sy[shotMultiPos[bay_i]-1];
		}

		if (weapon->sx[shotMultiPos[bay_i]-1] > 100)
		{
			shot->shotXM = weapon->sx[shotMultiPos[bay_i]-1];
			shot->shotX -= player[shot->playerNumber-1].delta_x_shot_move;
			if (shot->shotXM == 101)
				shot->shotY -= player[shot->playerNumber-1].delta_y_shot_move;
		}

		// Endless: a special pinned to the ship on both axes and spun by circlesize is a shield ring.
		// See doc/notes.md#special-pickups.
		if (endlessFxActive() && (bay_i == SHOT_SPECIAL || bay_i == SHOT_SPECIAL2) &&
		    shot->shotComplicated && shot->shotXM > 100 && shot->shotYM > 100)
		{
			shot->shotX -= weapon->bx[shotMultiPos[bay_i]-1]
			             + shot_circle_center_offset_px(shot->shotDevX, shot->shotDirX, shot->shotCirSizeX);
			shot->shotY -= weapon->by[shotMultiPos[bay_i]-1]
			             + shot_circle_center_offset_px(shot->shotDevY, shot->shotDirY, shot->shotCirSizeY);
		}

		// High-Velocity Rounds scales real, nonzero velocities on both axes. Keep
		// results moving and below the ship-attachment sentinel range.
		if (endlessFxActive())
		{
			int spd = endlessPerkShotSpeedPercent();
			if (spd != 100)
			{
				if (shot->shotXM != 0 && abs(shot->shotXM) < 100)
				{
					int m = (abs(shot->shotXM) * spd + 50) / 100;
					m = m < 1 ? 1 : (m > 99 ? 99 : m);
					shot->shotXM = shot->shotXM < 0 ? -m : m;
				}
				if (shot->shotYM != 0 && abs(shot->shotYM) < 100)
				{
					int m = (abs(shot->shotYM) * spd + 50) / 100;
					m = m < 1 ? 1 : (m > 99 ? 99 : m);
					shot->shotYM = shot->shotYM < 0 ? -m : m;
				}
			}
		}

		// Endless Guidance Package: steer the shots the weapon table would not, and tighten the ones
		// it already does. A shot that cannot be steered keeps its weapon-table aim.
		const int guidance_delay = endlessFxActive()
		    ? endlessPerkGuidanceDelay(bay_i, weapon->aim > 5 ? weapon->aim - 5 : 0) : 0;
		if (guidance_delay > 0 && shot_guidance_can_steer(shot))
		{
			// A beam pinned by the lone x sentinel (Laser, SDF Main Gun) re-encodes as riding
			// velocities: the same motion, now steerable on each axis, clamped into the band.
			if (shot->shotXM == SHOT_ATTACHED_VEL_MIN && shot->shotYM < SHOT_ATTACHED_VEL_MIN)
			{
				int v = SHOT_ATTACHED_VEL_REST + shot->shotYM;
				if (v < SHOT_ATTACHED_VEL_MIN)
					v = SHOT_ATTACHED_VEL_MIN;
				else if (v > SHOT_ATTACHED_VEL_MAX)
					v = SHOT_ATTACHED_VEL_MAX;
				shot->shotXM = SHOT_ATTACHED_VEL_REST;
				shot->shotYM = (JE_integer)v;
			}
			shot->aimAtEnemy = shot_guidance_nearest(shot->shotX, shot->shotY);
			shot->aimDelay = 5;
			shot->aimDelayMax = (JE_byte)(guidance_delay | SHOT_AIM_GUIDANCE);
		}
		else if (weapon->aim > 5)  /*Guided Shot*/
		{
			uint best_dist = 65000;
			JE_byte closest_enemy = 0;
			/*Find Closest Enemy*/
			for (x = 0; x < 100; x++)
			{
				if (enemyAvail[x] != 1 && !enemy[x].scoreitem)
				{
					// Guided Aim measures the same screen x the shot will steer toward.
					const int enemy_x = enemy[x].ex + (guidedShotScreenAim ? enemy[x].mapoffset : 0);
					y = abs(enemy_x - shot->shotX) + abs(enemy[x].ey - shot->shotY);
					if (y < best_dist)
					{
						best_dist = y;
						closest_enemy = x + 1;
					}
				}
			}
			shot->aimAtEnemy = closest_enemy;
			shot->aimDelay = 5;
			shot->aimDelayMax = weapon->aim - 5;
		}
		else
		{
			shot->aimAtEnemy = 0;
			shot->aimDelayMax &= SHOT_AIM_DELAY_MASK;  // a recycled slot must not keep the guidance bit
		}

		shotRepeat[bay_i] = weapon->shotrepeat;
		// Endless Evil Turbodrive/Overdrive curse: JAM the guns by lengthening the cooldown as the
		// kill combo climbs (0 unless an evil kill-fire window is up). Main/sidekick guns only; the
		// special bays run their own cadence (see varz.c). shotRepeat is a byte, so clamp.
		if (endlessFxActive() && bay_i != SHOT_SPECIAL && bay_i != SHOT_SPECIAL2)
		{
			const int jam = endlessKillFireJamTicks();
			if (jam > 0)
			{
				const int v = shotRepeat[bay_i] + jam;
				shotRepeat[bay_i] = (v > 250) ? 250 : (JE_byte)v;
			}
		}
	}

	return shot_id;
}

/* Endless Twin Pods, contract in shots.h: a second full player_shot_create from the same gun,
 * so it pays the generator again and takes the pattern's next position. See doc/notes.md#perks. */
JE_integer player_shot_create_twin(JE_integer first, JE_word portNum, uint sidekick, int twinDx,
                                   int x, int y, JE_word mouseX, JE_word mouseY, JE_word wpNum,
                                   JE_byte playerNum)
{
	if (first >= MAX_PWEAPON || twinDx == 0)
		return MAX_PWEAPON;

	const uint bay = (sidekick == LEFT_SIDEKICK) ? SHOT_LEFT_SIDEKICK : SHOT_RIGHT_SIDEKICK;
	return player_shot_create(portNum, bay, (JE_word)(x + twinDx), (JE_word)y, mouseX, mouseY, wpNum, playerNum);
}

/* Deflected shots keep the incoming art and tier tint while reversing velocity and acceleration.
 * Damage follows the normal player-shot path; see doc/notes.md#perks. */
#define DEFLECT_MIN_SPEED   4    // px per tick straight up, for a bullet that had come to rest
#define DEFLECT_LIFE_TICKS  255  // the pool countdown; the screen cull retires it before that

// A player-shot velocity past 100 rides the ship (SHOT_ATTACHED_VEL_MIN); a returned bullet never may.
static JE_integer deflect_velocity(int v)
{
	return (JE_integer)(v > 99 ? 99 : (v < -99 ? -99 : v));
}

JE_integer player_shot_create_deflected(const EnemyShotType *incoming, int damage, JE_byte playerNum)
{
	// The salvo window read below belongs to the deflecting ship, as it does in player_shot_create.
	endlessSetFxPlayer(playerNum >= 1 ? (uint)playerNum - 1 : 0);

	// A frame past 60000 is drawn from the special-weapon table, which no bullet belongs to.
	if (damage <= 0 || incoming->sgr >= 60000)
		return MAX_PWEAPON;

	int shot_id;
	for (shot_id = 0; shot_id < MAX_PWEAPON; shot_id++)
		if (shotAvail[shot_id] == 0)
			break;
	if (shot_id == MAX_PWEAPON)
		return MAX_PWEAPON;

	PlayerShotDataType *shot = &playerShotData[shot_id];
	shot->shotX = incoming->sx;
	shot->shotY = incoming->sy;
	shot->shotXM = deflect_velocity(-incoming->sxm);
	shot->shotYM = deflect_velocity(-incoming->sym);
	shot->shotXC = (JE_integer)-incoming->sxc;
	shot->shotYC = (JE_integer)-incoming->syc;
	if (shot->shotXM == 0 && shot->shotYM == 0)
		shot->shotYM = -DEFLECT_MIN_SPEED;

	shot->shotComplicated = 0;
	shot->shotDevX = 0;
	shot->shotDirX = 0;
	shot->shotDevY = 0;
	shot->shotDirY = 0;
	shot->shotCirSizeX = 0;
	shot->shotCirSizeY = 0;
	shot->shotTrail = 255;

	// The player draw takes frames above 500 from the second sheet, the enemy draw from 500 up, and
	// no shipped bullet sits on 500, so the sprite carries over as it is. The enemy loop wraps its
	// frame at animax, the player loop at shotAniMax.
	shot->shotGr = incoming->sgr;
	shot->shotAni = incoming->animate;
	shot->shotAniMax = (incoming->animax > 0) ? incoming->animax : 1;
	shot->tint = incoming->filter;   // an elite's or champion's bullet keeps its tier bank
	shot->shotDmg = (Uint8)damage;
	shot->shotBlastFilter = (JE_byte)((incoming->sgr > 500)
	                                  ? sprite2_dominant_bank(spriteSheet12, incoming->sgr - 500)
	                                  : sprite2_dominant_bank(spriteSheet8, incoming->sgr)) << 4;
	shot->chainReaction = 0;
	shot->playerNumber = playerNum;
	shot->aimAtEnemy = 0;
	shot->aimDelay = 0;
	shot->aimDelayMax = 0;
	// A deflection leaves the ship inside a charged window like anything else fired in one, so it
	// takes the tag, its damage bonus and its spark cue.
	shot->salvoBoost = (endlessFxActive() && endlessOpeningSalvoVolleyActive()) ? 1 : 0;
	shot->pierceLock = 0;
	shot->pierceLockCarry = 0;
	shot->pierceLockPending = 0;
	shot->pierceDmgCarry = 0;

	shotAvail[shot_id] = DEFLECT_LIFE_TICKS;
	return shot_id;
}

// A chain-reaction carrier deals no damage of its own: it is consumed on impact and replaced by
// the weapon named in its attack byte, which is what the enemy actually takes.
JE_integer player_shot_create_chained(JE_word PX, JE_word PY, JE_word mouseX, JE_word mouseY,
                                      JE_word wpNum, JE_byte playerNum, bool salvoBoost)
{
	salvoBoostOverride = true;
	salvoBoostFromParent = salvoBoost;
	const JE_integer shot_id = player_shot_create(0, SHOT_MISC, PX, PY, mouseX, mouseY, wpNum, playerNum);
	salvoBoostOverride = false;
	salvoBoostFromParent = false;
	return shot_id;
}
