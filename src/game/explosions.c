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

static float explosionRumble;

void explosion_rumble_add(float energy)
{
	explosionRumble += energy;
}

float explosion_rumble_take(void)
{
	const float energy = explosionRumble;
	explosionRumble = 0.0f;
	return energy;
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
	} explosion_data[54] /* [1..54] */ = {
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
		explosion_rumble_add(exploNum > 10 ? EXPLOSION_RUMBLE_BIG : EXPLOSION_RUMBLE_LARGE);

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
