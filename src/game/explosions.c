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
#include "explosions.h"

#include "net_style.h"
#include "opentyr.h"
#include "player.h"
#include "player_ship.h"

#include <math.h>
#include <string.h>

/*ExplosionData*/
Explosion explosions[MAX_EXPLOSIONS]; /* [1..ExplosionMax] */
JE_integer explosionFollowAmountX, explosionFollowAmountY;
Uint8 explosionFilter;  /* see explosions.h */
Uint8 explosionOpacity = NET_STYLE_SOLID;  /* see explosions.h */

static Uint8 explosionOpacityBySlot[MAX_EXPLOSIONS];

Uint8 explosion_opacity(uint slot)
{
	return (slot < COUNTOF(explosionOpacityBySlot) && explosionOpacityBySlot[slot] != 0)
	       ? explosionOpacityBySlot[slot] : NET_STYLE_SOLID;
}

/*Repeating Explosions*/
rep_explosion_type rep_explosions[MAX_REPEATING_EXPLOSIONS]; /* [1..20] */

// Blast rumble keeps these shares of its level each 35 Hz tick: about half a second from a
// full heavy blast to silence, a tenth of one for a light pop.
#define BLAST_FADE_HEAVY 0.82f
#define BLAST_FADE_LIGHT 0.55f
#define BLAST_CROSS      0.5f    // the share of each kind that also drives the other motor
#define BLAST_FLOOR      0.03f   // a fading level this low is silence
#define BLAST_NEAR_PX    160.0f  // from here on a ship feels only BLAST_FAR_SHARE of a blast
#define BLAST_FAR_SHARE  0.35f
#define BLAST_HOLD_MS    80      // the motors stop this long after the ticks do

static float blastPending[2][2];  // [ship][heavy, light] blasts of this tick
static float blastLevel[2][2];    // [ship][heavy, light] fading rumble felt now
static bool blastFelt;            // the last tick held a level above zero

void explosion_rumble_add(float energy, bool heavy, int x, int y)
{
	for (uint seat = 0; seat < COUNTOF(blastPending); ++seat)
	{
		const float dx = (float)(x - player[seat].x), dy = (float)(y - player[seat].y);
		const float closeness = MAX(0.0f, 1.0f - sqrtf(dx * dx + dy * dy) / BLAST_NEAR_PX);
		const float share = BLAST_FAR_SHARE + (1.0f - BLAST_FAR_SHARE) * closeness;
		blastPending[seat][heavy ? 0 : 1] += energy * share;
	}
}

void explosion_rumble_tick(bool live)
{
	if (live)
	{
		static const float fade[2] = { BLAST_FADE_HEAVY, BLAST_FADE_LIGHT };
		float low[2], high[2];
		bool felt = false;
		for (uint seat = 0; seat < COUNTOF(blastLevel); ++seat)
		{
			float *const level = blastLevel[seat];
			for (uint kind = 0; kind < 2; ++kind)
			{
				level[kind] = MIN(level[kind] * fade[kind] + blastPending[seat][kind], 1.0f);
				if (level[kind] < BLAST_FLOOR)
					level[kind] = 0.0f;
			}
			low[seat] = MIN(level[0] + BLAST_CROSS * level[1], 1.0f);
			high[seat] = MIN(level[1] + BLAST_CROSS * level[0], 1.0f);
			felt = felt || level[0] > 0.0f || level[1] > 0.0f;
		}

		// One more call after the fade ends lets the motors stop at once.
		if (felt || blastFelt)
			player_rumble_level(low, high, BLAST_HOLD_MS);
		blastFelt = felt;
	}

	memset(blastPending, 0, sizeof(blastPending));
}

void explosion_rumble_reset(void)
{
	memset(blastPending, 0, sizeof(blastPending));
	memset(blastLevel, 0, sizeof(blastLevel));
	blastFelt = false;
}

void JE_setupExplosion(
	JE_integer x,
	JE_integer y,
	JE_integer deltaY,
	JE_integer type,
	bool fixedPosition,  // true when coin/gem value
	bool followPlayer)   // true when player shield (1P only)
{
	const struct {
		JE_word sprite;
		JE_byte ttl;
	} explosion_data[EXPLOSION_TYPES] /* [1..54] */ = {
		{ 144,  7 },
		{ 120, 12 },
		{ 190, 12 },
		{ 209, 12 },
		{ 152, 12 },
		{ 171, 12 },
		{ 133,  7 },   /*White Smoke*/
		{   1, 12 },
		{  20, 12 },
		{  39, 12 },
		{  58, 12 },
		{ 110,  3 },
		{  76,  7 },
		{  91,  3 },
/*15*/	{ 227,  3 },
		{ 230,  3 },
		{ 233,  3 },
		{ 252,  3 },
		{ 246,  3 },
/*20*/	{ 249,  3 },
		{ 265,  3 },
		{ 268,  3 },
		{ 271,  3 },
		{ 236,  3 },
/*25*/	{ 239,  3 },
		{ 242,  3 },
		{ 261,  3 },
		{ 274,  3 },
		{ 277,  3 },
/*30*/	{ 280,  3 },
		{ 299,  3 },
		{ 284,  3 },
		{ 287,  3 },
		{ 290,  3 },
/*35*/	{ 293,  3 },
		{ 165,  8 },   /*Coin Values*/
		{ 184,  8 },
		{ 203,  8 },
		{ 222,  8 },
		{ 168,  8 },
		{ 187,  8 },
		{ 206,  8 },
		{ 225, 10 },
		{ 169, 10 },
		{ 188, 10 },
		{ 207, 20 },
		{ 226, 14 },
		{ 170, 14 },
		{ 189, 14 },
		{ 208, 14 },
		{ 246, 14 },
		{ 227, 14 },
		{ 265, 14 },
		{  96,  3 }
	};

	if (type != 98 && type != 198 && (type < 0 || type >= EXPLOSION_TYPES))
		return;

	if (y > -16 && y < 190)
	{
		for (int i = 0; i < MAX_EXPLOSIONS; i++)
		{
			if (explosions[i].ttl == 0)
			{
				explosions[i].x = x;
				explosions[i].y = y;
				if (type == 6)
				{
					explosions[i].y += 12;
					explosions[i].x += 2;
				}
				else if (type == 98 || type == 198)
				{
					type = 6;
				}
				explosions[i].sprite = explosion_data[type].sprite;
				explosions[i].ttl = explosion_data[type].ttl;
				explosions[i].followPlayer = followPlayer;
				explosions[i].fixedPosition = fixedPosition;
				explosions[i].deltaY = deltaY;
				explosions[i].filter = explosionFilter;
				explosionOpacityBySlot[i] = explosionOpacity;
				explosions[i].id_gen++;  // distinct interpolation id for this reuse of the slot
				break;
			}
		}
	}
}

void JE_setupExplosionLarge(JE_boolean enemyGround, JE_byte exploNum, JE_integer x, JE_integer y)
{
	if (y >= 0)
	{
		explosion_rumble_add(exploNum > 10 ? EXPLOSION_RUMBLE_BIG : EXPLOSION_RUMBLE_LARGE,
		                     true, x, y);

		if (enemyGround)
		{
			JE_setupExplosion(x - 6, y - 14, 0,  2, false, false);
			JE_setupExplosion(x + 6, y - 14, 0,  4, false, false);
			JE_setupExplosion(x - 6, y,      0,  3, false, false);
			JE_setupExplosion(x + 6, y,      0,  5, false, false);
		}
		else
		{
			JE_setupExplosion(x - 6, y - 14, 0,  7, false, false);
			JE_setupExplosion(x + 6, y - 14, 0,  9, false, false);
			JE_setupExplosion(x - 6, y,      0,  8, false, false);
			JE_setupExplosion(x + 6, y,      0, 10, false, false);
		}

		bool big;

		if (exploNum > 10)
		{
			exploNum -= 10;
			big = true;
		}
		else
		{
			big = false;
		}

		if (exploNum)
		{
			for (int i = 0; i < MAX_REPEATING_EXPLOSIONS; i++)
			{
				if (rep_explosions[i].ttl == 0)
				{
					rep_explosions[i].ttl = exploNum;
					rep_explosions[i].delay = 2;
					rep_explosions[i].x = x;
					rep_explosions[i].y = y;
					rep_explosions[i].big = big;
					rep_explosions[i].filter = explosionFilter;
					break;
				}
			}
		}
	}
}
