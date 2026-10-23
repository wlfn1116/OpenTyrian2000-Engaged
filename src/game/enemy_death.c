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
#include "enemy_death.h"

#include "config.h"
#include "debug_flags.h"
#include "endless.h"
#include "episodes.h"
#include "explosions.h"
#include "game_loop.h"
#include "game_state.h"
#include "health_bars.h"
#include "level_events.h"
#include "mtrand.h"
#include "player.h"
#include "qa.h"
#include "rollback.h"
#include "sim_math.h"
#include "sndmast.h"
#include "sparks.h"
#include "video.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Enemy-shot mechanics stay with the pool; modifier policy lives in endless_combat.c.

// Leave this many enemy-shot slots free before allowing a Martyrdom burst.
#define ENDLESS_MARTYR_POOL_MARGIN   48
#define ENDLESS_MARTYR_SHOT_SPEED    3.0f  // slow bullets, as specified ("four slow cardinal shots")
#define ENDLESS_MARTYR_SHOT_DMG      4     // base per-bullet damage, scaled by the sector's shot-damage ramp
#define ENDLESS_MARTYR_SHOT_DURATION 120   // ticks before a burst bullet self-expires (also culled off-screen)
#define ENDLESS_TWO_PI               6.28318531f

// The playfield box an enemy's sprite covers: an ordinary enemy is one 12x14 cell at
// (ex + mapoffset, ey), a 2x2 enemy four of them around that point. Same geometry the draw uses.
static void enemy_sprite_box(unsigned int i, int *left, int *top, int *right, int *bottom)
{
	const bool big = (enemy[i].size == 1);
	*left   = enemy[i].ex + enemy[i].mapoffset + (big ? -6 : 0);
	*top    = enemy[i].ey + (big ? -7 : 0);
	*right  = *left + (big ? 24 : 12);
	*bottom = *top  + (big ? 28 : 14);
}

/* The visible screen bounds of the linked or 2x2 body `i` belongs to; the freed slot is measured
 * directly before unioning live link partners. Returns false for a lone single-cell enemy. */
static bool enemy_hull_bounds(unsigned int i, int *left, int *top, int *right, int *bottom)
{
	enemy_sprite_box(i, left, top, right, bottom);
	bool multiCell = (enemy[i].size == 1);

	const JE_byte link = enemy[i].linknum;
	if (link != 0)
	{
		for (unsigned int e = 0; e < COUNTOF(enemy); ++e)
		{
			if (e == i || enemyAvail[e] == 1 || enemy[e].linknum != link)
				continue;

			int l, t, r, b;
			enemy_sprite_box(e, &l, &t, &r, &b);

			// Measure only visible parts; boss anchors may sit above the playfield.
			if (r <= PLAYFIELD_LEFT || l > PLAYFIELD_RIGHT || b <= 0 || t >= vga_height)
				continue;

			if (l < *left)   *left = l;
			if (t < *top)    *top = t;
			if (r > *right)  *right = r;
			if (b > *bottom) *bottom = b;
			multiCell = true;
		}
	}

	return multiCell;
}

// Center Martyrdom on the visible bounds of a linked or 2x2 body.
static void endlessMartyrBurstOrigin(unsigned int i, JE_integer *ox, JE_integer *oy)
{
	int left, top, right, bottom;
	if (!enemy_hull_bounds(i, &left, &top, &right, &bottom))
	{
		*ox = enemy[i].ex + enemy[i].mapoffset;
		*oy = enemy[i].ey;
		return;
	}

	*ox = (JE_integer)((left + right) / 2);
	*oy = (JE_integer)((top + bottom) / 2);
}

/* Contract in enemy_death.h. Bank ex space because the per-tick mapoffset reapplies: a screen-space
 * hull center must shed the dying slot's offset. */
void enemy_loot_anchor(unsigned int i, JE_integer *x, JE_integer *y)
{
	int left, top, right, bottom;
	if (!enemy_hull_bounds(i, &left, &top, &right, &bottom))
	{
		*x = enemy[i].ex;
		*y = enemy[i].ey;
		return;
	}

	*x = (JE_integer)((left + right) / 2 - 6 - enemy[i].mapoffset);
	*y = (JE_integer)((top + bottom) / 2 - 7);
}

// Spawn a 4-, 6-, or 8-way Martyrdom burst unless the shared enemy-shot pool is nearly full.
// Every burst uses the fixed Martyrdom sprite recolored by `tint`.
static void endlessSpawnMartyrBurst(JE_integer sx, JE_integer sy, int shots, Uint8 tint)
{
	if (shots <= 0)
		return;
	if (endlessReviveGraceActive())
		return;                          // revive grace: no enemy bullets enter the field, death bursts included
	const JE_word sgr = endlessMartyrShotSprite();

	int freeSlots = 0;
	for (int b = 0; b < ENEMY_SHOT_MAX; ++b)
		if (enemyShotAvail[b] == 1)
			++freeSlots;
	if (freeSlots < shots + ENDLESS_MARTYR_POOL_MARGIN)
		return;                          // pool nearly full; suppress the burst

	int dmg = (ENDLESS_MARTYR_SHOT_DMG * endlessShotDamagePercent() + 50) / 100;
	dmg = (dmg < 1) ? 1 : (dmg > 255) ? 255 : dmg;

	for (int k = 0; k < shots; ++k)
	{
		int b;
		for (b = 0; b < ENEMY_SHOT_MAX; ++b)
			if (enemyShotAvail[b] == 1)
				break;
		if (b == ENEMY_SHOT_MAX)
			return;                      // pool exhausted mid-burst (shouldn't happen past the guard)
		enemyShotAvail[b] = 0;           // occupy the slot

		const float ang = ENDLESS_TWO_PI * (float)k / (float)shots;
		enemyShot[b].sx  = sx;
		enemyShot[b].sy  = sy;
		enemyShot[b].sxm = (JE_integer)roundf(sim_cosf(ang) * ENDLESS_MARTYR_SHOT_SPEED);
		enemyShot[b].sym = (JE_integer)roundf(sim_sinf(ang) * ENDLESS_MARTYR_SHOT_SPEED);
		enemyShot[b].sxc = 0;
		enemyShot[b].syc = 0;
		enemyShot[b].tx = 0;
		enemyShot[b].ty = 0;
		enemyShot[b].sgr = sgr;
		enemyShot[b].sdmg = (JE_byte)dmg;
		enemyShot[b].duration = ENDLESS_MARTYR_SHOT_DURATION;
		enemyShot[b].animate = 0;
		enemyShot[b].animax = 0;
		// Radial burst shots never course-correct.
		enemyShot[b].seekerArm = 0;
		enemyShot[b].seekerLeft = 0;
		enemyShot[b].filter = tint;
	}
}

// SHOCKWAVE (endless boon): an elite/champion death vaporises every enemy bullet within
// `radius` of it (a negative radius clears the whole field; what a boss bar emptying does).
void endlessShockwaveClear(JE_integer sx, JE_integer sy, int radius)
{
	if (radius == 0)
		return;
	bool caught = false;
	for (int b = 0; b < ENEMY_SHOT_MAX; ++b)
	{
		if (enemyShotAvail[b])
			continue;
		if (radius > 0 && (abs(enemyShot[b].sx - sx) > radius || abs(enemyShot[b].sy - sy) > radius))
			continue;
		enemy_shot_vaporise_sparks(b);
		JE_setupExplosion(enemyShot[b].sx, enemyShot[b].sy, 0, 0, false, false);
		enemyShotAvail[b] = true;
		caught = true;
	}
	if (caught)
		soundQueue[4] = S_WEAPON_7;   // the same point-defense "thunk" the Countermeasure burst uses
}

// Drain Chain Reaction after the player-shot pass so its link walk stays stable. Cascade pulses
// run on the following tick.
#define CHAIN_QUEUE_MAX 64
static struct { int x, y; } chainPulse[CHAIN_QUEUE_MAX];
static int chainPulseN = 0;
static int chainPulseLastLink = 0;   // dedup consecutive same-link kills -> one pulse per multi-tile enemy

// Owner, salvo, and wave travel with every queued pulse and are rollback state.
static int chainPulseOwner[CHAIN_QUEUE_MAX];

// Opening Salvo state at the kill that began this pulse.
static bool chainPulseSalvo[CHAIN_QUEUE_MAX];

// Wave serial used to limit a wave to one hit per linked hull.
static JE_word chainPulseWave[CHAIN_QUEUE_MAX];

// Tick-local state inherited by cascade pulses queued during a drain.
#define CHAIN_DRAIN_IDLE (-1)
static int chainDrainSalvo = CHAIN_DRAIN_IDLE;
static JE_word chainDrainWave = 0;

// Evaluate a pulse under `owner`'s perks rather than the ambient effect player. Returns the context
// to restore, as the elite payout does in endless_combat.c.
static uint chain_fx_enter(int owner)
{
	const uint saved = endlessFxPlayer();
	endlessSetFxPlayer((uint)owner);
	return saved;
}

void chain_reset_queue(void)
{
	chainPulseN = 0;
	chainPulseLastLink = 0;
	chainDrainSalvo = CHAIN_DRAIN_IDLE;
}

// Append a pulse of `wave` belonging to `owner`.
static void chain_queue_at(int screenX, int y, int owner, bool salvo, JE_word wave)
{
	if (chainPulseN < CHAIN_QUEUE_MAX)
	{
		chainPulse[chainPulseN].x = screenX;
		chainPulse[chainPulseN].y = y;
		chainPulseOwner[chainPulseN] = owner;
		chainPulseSalvo[chainPulseN] = salvo;
		chainPulseWave[chainPulseN] = wave;
		++chainPulseN;
	}
}

// Queue a pulse at a killed enemy's screen position. Deduped per linked enemy exactly like
// endlessCountKill, so a multi-tile enemy pulses once, not once per tile.
static void chain_queue_kill(int screenX, int y, int linknum, int killer)
{
	const int owner = (int)endlessPerkChainOwner(killer);
	const bool inDrain = (chainDrainSalvo != CHAIN_DRAIN_IDLE);

	const uint fxSaved = chain_fx_enter(owner);
	const bool active = endlessPerkChainReactionActive();
	// A cascade inherits its parent tag. A fresh kill reads the owner's live salvo window.
	const bool salvo = inDrain ? (chainDrainSalvo != 0) : endlessOpeningSalvoVolleyActive();
	endlessSetFxPlayer(fxSaved);

	if (!active)
		return;
	if (linknum != 0 && linknum == chainPulseLastLink)
		return;
	chainPulseLastLink = linknum;
	chain_queue_at(screenX, y, owner, salvo, inDrain ? chainDrainWave : enemyKilled);
}

// The tier multiplier this hull's damage is divided by, in ENEMY_DAMAGE_ACCUM_SCALE units: Nx
// boss HP (expert mode and/or endless depth) combined with the endless elite/champion tier.
static int enemy_hp_multiplier100(unsigned int slot)
{
	const bool has_boss_bar = enemy_has_boss_bar(enemy[slot].linknum);

	int bossHpMult100 = ENEMY_DAMAGE_ACCUM_SCALE;
	if (expertMode)
		bossHpMult100 *= expertBossHpMult;
	if (endlessFxActive())
		bossHpMult100 = bossHpMult100 * endlessBossHpMult100() / ENEMY_DAMAGE_ACCUM_SCALE;

	if (!endlessFxActive())
		return has_boss_bar ? bossHpMult100 : ENEMY_DAMAGE_ACCUM_SCALE;
	return endlessEnemyHpMult100(has_boss_bar, bossHpMult100, enemy[slot].eliteState);
}

// The whole-x reading of the same figure, which is what the pierce delay is calibrated against.
int enemy_hp_multiplier(unsigned int slot)
{
	return enemy_hp_multiplier100(slot) / ENEMY_DAMAGE_ACCUM_SCALE;
}

// Combined tier and overflow divisor, in hundredths.
int enemy_hp_divisor100(unsigned int slot)
{
	return enemy_hp_multiplier100(slot) * endlessArmorOverflow100() / ENEMY_DAMAGE_ACCUM_SCALE;
}

// Spend raw damage through a hull's divisor and carry the remainder.
int enemy_spend_damage(unsigned int slot, int damage)
{
	const int divisor100 = enemy_hp_divisor100(slot);
	if (divisor100 <= 100)
		return damage;

	enemy[slot].damageAccum += damage * ENEMY_DAMAGE_ACCUM_SCALE;
	const int spent = enemy[slot].damageAccum / divisor100;
	enemy[slot].damageAccum -= spent * divisor100;
	return spent;
}

// Resolve a destroyed enemy's replacement body and payout.
static void enemy_death_payout(unsigned int slot, int payee)
{
	if ((enemy[slot].enemydie > 0) &&
	    !((superArcadeMode != SA_NONE) &&
	      (enemyDat[enemy[slot].enemydie].value == 30000)))
	{
		const JE_word dieType = enemy[slot].enemydie;
		int enemy_offset = (int)slot - ((int)slot % 25);
		if (enemyDat[dieType].value > 30000)
		{
			enemy_offset = 0;
		}
		const int b = JE_newEnemy(enemy_offset, dieType, 0);
		if (b != 0)
		{
			if ((superArcadeMode != SA_NONE) && (enemy[b-1].evalue > 30000))
			{
				// Random Pickups stays within this ship's five Super Arcade weapons.
				if (arcadeSuperPickupRandomActive())
					superArcadePowerUp = (mt_rand() % 5) + 1;
				else
				{
					superArcadePowerUp++;
					if (superArcadePowerUp > 5)
						superArcadePowerUp = 1;
				}
				enemy[b-1].egr[1-1] = 5 + superArcadePowerUp * 2;
				enemy[b-1].evalue = 30000 + superArcadePowerUp;
			}

			if (enemy[b-1].evalue != 0)
				enemy[b-1].scoreitem = true;
			else
				enemy[b-1].scoreitem = false;

			enemy[b-1].ex = enemy[slot].ex;
			enemy[b-1].ey = enemy[slot].ey;

			// Transformed bodies inherit their tier; loot remains untiered.
			if (enemyAvail[b-1] != 2)
				enemy[b-1].eliteState = enemy[slot].eliteState;
		}
	}

	if ((enemy[slot].evalue > 0) && (enemy[slot].evalue < 10000))
	{
		if (enemy[slot].evalue == 1)
		{
			if (endlessMode)  // datacube on a shot enemy -> 5000 gem in endless (no cube archive)
				endlessDropCubeGem(slot);
			else
				cubeMax++;
		}
		else
		{
			player_award_kill_cash(&player[payee], enemy[slot].evalue);
		}
	}
}

bool enemy_is_wreck(unsigned int slot)
{
	return enemyAvail[slot] == 2 && enemy[slot].edamaged;
}

Uint8 enemy_body_tint(unsigned int slot)
{
	if (enemy_is_wreck(slot))
		return 0;
	return (enemy[slot].eliteState >= 2)
	       ? endlessEliteTint(enemy[slot].eliteState)
	       : endlessEliteShellTint(enemy[slot].linknum, enemy[slot].armorleft);
}

// Resolve one hull tile through its normal death or staged transform path.
static void enemy_part_destroy(unsigned int slot, int payee, int killer, bool staged)
{
	const int screenX = enemy[slot].ex + enemy[slot].mapoffset;

	if (enemy[slot].special)
	{
		assert((unsigned int) enemy[slot].flagnum-1 < COUNTOF(globalFlags));
		globalFlags[enemy[slot].flagnum-1] = enemy[slot].setto;
	}

	enemy_death_payout(slot, payee);

	if (staged && endlessHomingTierActive())
	{
		// Remove enemy corpses so they don't hang around the player for the rest of the level.
		enemyAvail[slot] = 1;
	}
	else if (staged)
	{
		enemy[slot].edlevel = 0;
		enemyAvail[slot] = 2;
		enemy[slot].egr[1-1] = enemy[slot].edgr;
		enemy[slot].ani = 1;
		enemy[slot].aniactive = 0;
		enemy[slot].animax = 0;
		enemy[slot].animin = 1;
		enemy[slot].edamaged = true;
		enemy[slot].enemycycle = 1;
	}
	else
	{
		// Tally, bounty, SHOCKWAVE, MARTYRDOM and the Chain Reaction pulse all live in
		// the one helper; never inline any of them here again (see enemy_death.h).
		enemy_logical_death(slot, killer);
	}

	explosionFilter = endlessEliteTint(enemy[slot].eliteState);
	if (enemyDat[enemy[slot].enemytype].esize == 1)
	{
		JE_setupExplosionLarge(enemy[slot].enemyground, enemy[slot].explonum, screenX, enemy[slot].ey);
		soundQueue[6] = S_EXPLOSION_9;
	}
	else
	{
		JE_setupExplosion(screenX, enemy[slot].ey, 0, 1, false, false);
		soundQueue[6] = S_EXPLOSION_8;
	}
	explosionFilter = 0;
}

/* Which parts a killing blow on `slot` takes with it: the slot itself, everything a link-254
 * blow touches, and the parts its nonzero link names by the level's three linking rules. */
static bool enemy_kill_group_matches(unsigned int slot, unsigned int part, JE_byte link, bool *staged)
{
	const JE_byte partLink = enemy[part].linknum;
	*staged = enemy[part].edlevel == -1 && link == partLink;
	if (part == slot || link == 254)
		return true;
	return link != 255
	    && (link == partLink || link - 100 == partLink
	        || (partLink > 40 && partLink / 20 == link / 20 && partLink <= link));
}

// Contract in enemy_death.h. The walk runs on the shared temp2/temp3 on purpose: a further hit
// of the same shot in the same tick reads what they hold, and this keeps the extraction exact.
void enemy_kill_group(unsigned int slot, int payee, int killer)
{
	JE_byte link = enemy[slot].linknum;
	if (link == 0)
		link = 255;

	if (link == 254 && superEnemy254Jump > 0)
		JE_eventJump(superEnemy254Jump);

	for (temp2 = 0; temp2 < 100; temp2++)
	{
		if (enemyAvail[temp2] == 1)
			continue;
		temp3 = enemy[temp2].linknum;
		bool staged;
		if (enemy_kill_group_matches(slot, temp2, link, &staged))
			enemy_part_destroy(temp2, payee, killer, staged);
	}
}

// Central kill path for tallies, link-group latches, bounties, and reactive effects.
// Despawns and collision removals are not kills.
void enemy_logical_death(unsigned int i, int killer)
{
	enemyAvail[i] = 1;
	enemyKilled++;

	const int linknum = enemy[i].linknum;
	const int elite = enemy[i].eliteState;
	const JE_integer sx = enemy[i].ex + enemy[i].mapoffset;
	const JE_integer sy = enemy[i].ey;

	// Latched bookkeeping: mandatory for every kill, elite or not. Each self-guards on
	// endlessFxActive(), so outside an endless run these cost a call and nothing else.
	endlessCountKill(linknum, killer);
	endlessAwardEliteKill(linknum, elite, killer);
	endlessPerkSurveyorDrops(i, linknum, killer);
	const int shockRadius = endlessShockwaveRadius(linknum, elite);
	const int martyrShots = endlessMartyrdomBurstShots(linknum, elite);

	// Clear existing fire before spawning Martyrdom shots.
	endlessShockwaveClear(sx, sy, shockRadius);
	if (martyrShots > 0)
	{
		JE_integer burstX, burstY;
		endlessMartyrBurstOrigin(i, &burstX, &burstY);
		endlessSpawnMartyrBurst(burstX, burstY, martyrShots, endlessEliteTint(elite));
	}
	chain_queue_kill(sx, sy, linknum, killer);
}

// Bank 15 keeps the Chain Reaction flash visible in every shipped palette.
#define CHAIN_FLASH_FILTER 0xF0

// Flash a chipped victim and the rest of its linked hull; JE_drawEnemy paints filter for one
// frame and clears it again.
static void chain_flash_enemy(unsigned int i)
{
	if (enemy[i].linknum == 0)
	{
		enemy[i].filter = CHAIN_FLASH_FILTER;
		return;
	}

	for (unsigned int g = 0; g < COUNTOF(enemy); ++g)
	{
		if (enemy[g].linknum == enemy[i].linknum && enemyAvail[g] != 1)
			enemy[g].filter = CHAIN_FLASH_FILTER;
	}
}

// The ring shows the blast radius; a bolt marks each hit. See doc/notes.md#gauges-and-effects.
#define CHAIN_RING_SPACING_PX   12   // between neighbouring sparks around the ring
#define CHAIN_RING_LIFE_TICKS    5
#define CHAIN_RINGS_PER_TICK    16
#define CHAIN_BOLT_SPACING_PX    4   // between sparks along the bolt: near-solid at this pitch
#define CHAIN_BOLT_LIFE_TICKS    5
#define CHAIN_BOLT_WANDER_PX     3   // how far the bolt bows off the straight line at its middle
#define CHAIN_BOLTS_PER_TICK    16

// A bolt is thin and gone in a few ticks, so it is lit well above the aura lift: the core clamps to
// the top of the bank and the halo around it stays a shade below, which reads as one bright line.
#define CHAIN_BOLT_BRIGHT       12

// Sprites are placed by their top-left corner, so both effects work from the centre of a 12px cell.
#define CHAIN_CELL_MID_PX        6

static void chain_ring_sparks(int x, int y, int radius, int pulse)
{
	if (rollback_resim_silent)
		return;

	JE_doSPRingSeeded((JE_word)(x + CHAIN_CELL_MID_PX), (JE_word)(y + CHAIN_CELL_MID_PX),
	                  (JE_word)radius, CHAIN_RING_SPACING_PX, CHAIN_FLASH_FILTER,
	                  CHAIN_RING_LIFE_TICKS, ENDLESS_SPARK_BRIGHT,
	                  ((Uint32)x << 20) ^ ((Uint32)y << 8) ^ (Uint32)pulse);
}

static void chain_bolt_sparks(int x0, int y0, int x1, int y1, int victim)
{
	if (rollback_resim_silent)
		return;

	JE_doSPBoltSeeded((JE_word)(x0 + CHAIN_CELL_MID_PX), (JE_word)(y0 + CHAIN_CELL_MID_PX),
	                  (JE_word)(x1 + CHAIN_CELL_MID_PX), (JE_word)(y1 + CHAIN_CELL_MID_PX),
	                  CHAIN_BOLT_SPACING_PX, CHAIN_BOLT_WANDER_PX, CHAIN_FLASH_FILTER,
	                  CHAIN_BOLT_LIFE_TICKS, CHAIN_BOLT_BRIGHT,
	                  ((Uint32)x1 << 20) ^ ((Uint32)y1 << 8) ^ (Uint32)victim);
}

// A wave may damage each eligible hull once.
static bool chain_target_eligible(int slot, JE_word wave)
{
	if (enemyAvail[slot] != 0)
		return false;                                  // live enemies only
	if (enemy[slot].scoreitem || enemy[slot].special)
		return false;                                  // pickups / flag-setters: never
	if (enemy[slot].armorleft == 0 || enemy[slot].armorleft >= 255)
		return false;                                  // dead, or invulnerable
	if (enemy[slot].chainWave == wave)
		return false;                                  // this wave has already landed on it
	return true;
}

// Treat a linked hull as one target. A reached hull takes one hit at its live center and marks all
// parts with `wave`; return false when the hull is out of range or already hit.
static bool chain_group_target(JE_byte linknum, int px, int py, int radius, JE_word wave,
                               int *out_x, int *out_y, int *out_victim)
{
	int minX = INT_MAX;
	int maxX = INT_MIN;
	int minY = INT_MAX;
	int maxY = INT_MIN;
	bool reached = false;

	for (int g = 0; g < 100; ++g)
	{
		if (enemy[g].linknum != linknum || !chain_target_eligible(g, wave))
			continue;

		const int gx = enemy[g].ex + enemy[g].mapoffset;
		if (gx < minX)
			minX = gx;
		if (gx > maxX)
			maxX = gx;
		if (enemy[g].ey < minY)
			minY = enemy[g].ey;
		if (enemy[g].ey > maxY)
			maxY = enemy[g].ey;

		if (abs(gx - px) <= radius && abs(enemy[g].ey - py) <= radius)
			reached = true;
	}

	if (!reached)
		return false;

	*out_x = (minX + maxX) / 2;
	*out_y = (minY + maxY) / 2;

	int best = -1;
	int bestDist = INT_MAX;
	for (int g = 0; g < 100; ++g)
	{
		if (enemy[g].linknum != linknum || !chain_target_eligible(g, wave))
			continue;
		enemy[g].chainWave = wave;

		const int d = abs((enemy[g].ex + enemy[g].mapoffset) - *out_x) + abs(enemy[g].ey - *out_y);
		if (d < bestDist)
		{
			bestDist = d;
			best = g;
		}
	}

	*out_victim = best;
	return best >= 0;
}

// Take a linked hull down whole, every live tile paying out, dying and exploding as it would
// under a killing shot.
static void chain_destroy_group(JE_byte linknum, int owner)
{
	for (int g = 0; g < 100; ++g)
	{
		if (enemy[g].linknum != linknum || enemyAvail[g] == 1)
			continue;

		enemy_part_destroy(g, owner, owner, enemy[g].edlevel == -1);
	}
}

// Drain one Chain Reaction hop. Each wave hits a hull once; destroyed targets queue the next hop.
// Pickups, flag setters, and invulnerable hulls stop propagation.
void chain_reaction_process(void)
{
	if (chainPulseN == 0)
	{
		chainPulseLastLink = 0;
		return;
	}
	// Show one impact effect per victim per drain, even when several waves hit.
	bool flashed[COUNTOF(enemy)] = { false };
	int rings = 0;
	int bolts = 0;
	bool anyHit = false;

	// This tick's hop is the stretch of queue standing at entry. Anything queued below is the next
	// hop and is left for the next tick.
	const int hop = chainPulseN;

	for (int p = 0; p < hop; ++p)
	{
		const int px = chainPulse[p].x, py = chainPulse[p].y;
		const JE_word wave = chainPulseWave[p];

		// Each pulse is measured under the perks of the ship that made its kill, and carries its own
		// salvo tag and wave both into that figure and on to whatever hops it queues.
		chainDrainSalvo = chainPulseSalvo[p] ? 1 : 0;
		chainDrainWave = wave;
		const uint fxSaved = chain_fx_enter(chainPulseOwner[p]);
		const int radius = endlessPerkChainRadius();
		const int dmg    = endlessPerkChainDamage(chainPulseSalvo[p]);
		endlessSetFxPlayer(fxSaved);

		for (int e = 0; e < 100; ++e)
		{
			if (!chain_target_eligible(e, wave))
				continue;

			const JE_byte linknum = enemy[e].linknum;
			const bool lone = (linknum == 0);

			int ex;               // where the blast lands
			int ey;
			int victim = e;       // the tile that takes it

			if (lone)
			{
				ex = enemy[e].ex + enemy[e].mapoffset;
				ey = enemy[e].ey;
				if (abs(ex - px) > radius || abs(ey - py) > radius)
					continue;
				enemy[e].chainWave = wave;
			}
			else if (!chain_group_target(linknum, px, py, radius, wave, &ex, &ey, &victim))
			{
				continue;   // out of range, or this wave has hit it; every tile answers the same
			}

			// A boss and an elite-tier hull spend damage through an accumulator, so the pulse pays
			// into that rather than chipping raw armor and bypassing the scaling.
			const int spend = enemy_spend_damage(victim, dmg);

			// Survives, or goes down: lone fodder on its own, a linked hull with every tile at once.
			if (enemy[victim].armorleft > spend)
			{
				enemy[victim].armorleft -= (JE_byte)spend;     // chipped, but survives
				if (spend > 0)
					enemy[victim].healthbar_seen = true;       // damage taken: show its health bar

				// Shown even on a tick the accumulator swallowed whole, or a heavily scaled hull
				// would take the blast in silence.
				anyHit = true;
				boss_bar_note_hit(linknum);
				if (!flashed[victim])
				{
					flashed[victim] = true;
					chain_flash_enemy(victim);
					JE_setupExplosion(ex, ey - 6, 0, 0, false, false);
					if (bolts < CHAIN_BOLTS_PER_TICK)
					{
						chain_bolt_sparks(px, py, ex, ey, victim);
						++bolts;
					}
				}
			}
			else
			{
				// Use the normal kill path so payout, tiers, and linked parts stay together.
				anyHit = true;
				if (bolts < CHAIN_BOLTS_PER_TICK)   // a kill is reached once, so it needs no latch
				{
					chain_bolt_sparks(px, py, ex, ey, victim);
					++bolts;
				}

				if (lone)
					enemy_part_destroy(e, chainPulseOwner[p], chainPulseOwner[p], false);
				else
					chain_destroy_group(linknum, chainPulseOwner[p]);
			}
		}

		// Draw every blast radius; impact feedback still requires a hit.
		if (rings < CHAIN_RINGS_PER_TICK)
		{
			chain_ring_sparks(px, py, radius, p);
			++rings;
		}
	}

	if (anyHit)
		soundQueue[5] = S_ENEMY_HIT;   // one per drain, in the slot an ordinary hit uses

	// Close the hop: what it queued becomes the next one, slid down to the front. The queue bound is
	// spelled out rather than inferred from chainPulseN, which the analyzer cannot follow this far.
	int carried = 0;
	for (int src = hop; src < chainPulseN && src < CHAIN_QUEUE_MAX; ++src)
	{
		chainPulse[carried] = chainPulse[src];
		chainPulseOwner[carried] = chainPulseOwner[src];
		chainPulseSalvo[carried] = chainPulseSalvo[src];
		chainPulseWave[carried] = chainPulseWave[src];
		++carried;
	}
	chainPulseN = carried;
	chainPulseLastLink = 0;
	chainDrainSalvo = CHAIN_DRAIN_IDLE;   // the next fresh kill reads its own window and wave again
}

/* Contract in qa.h. The driver lives here because the queue and the drain do; the assertions live
 * with the state they read. */
void qa_chain_kill_row(int owner, int evalue, int count,
                       JE_byte linknum, int eliteState, long *out_paid0, long *out_paid1,
                       int *out_killed, bool *out_dropped)
{
	const JE_byte dieType = 1;   // any spawnable body; the test only asks whether one appeared

	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;   // an empty field, so anything live afterwards came out of a death
	}

	const uint fxSaved = chain_fx_enter(owner);
	const int step = endlessPerkChainRadius() / 2;
	endlessSetFxPlayer(fxSaved);

	for (int i = 0; i < count; ++i)
	{
		enemyAvail[i] = 0;
		enemy[i].ex = (JE_integer)(100 + i * step);
		enemy[i].ey = 100;
		enemy[i].armorleft = 1;   // one pulse finishes it
		enemy[i].enemytype = 1;
		enemy[i].evalue = (Sint16)evalue;
		enemy[i].enemydie = dieType;
		enemy[i].linknum = linknum;   // 0 lays out lone fodder, anything else one linked hull
	}

	if (eliteState >= 2)
	{
		for (int i = 0; i < count; ++i)
			enemy[i].eliteState = (JE_byte)eliteState;
	}

	const Sint64 before0 = player[0].cash;
	const Sint64 before1 = player[1].cash;
	const JE_word killedBefore = enemyKilled;

	chain_reset_queue();
	chain_queue_at(enemy[0].ex, enemy[0].ey, owner, false, 1);
	for (int t = 0; t < CHAIN_QUEUE_MAX && chainPulseN > 0; ++t)
		chain_reaction_process();

	*out_paid0 = (long)(player[0].cash - before0);
	*out_paid1 = (long)(player[1].cash - before1);

	// Count deaths rather than empty slots: a drop is spawned into whatever slot is free, which
	// includes the ones the row just vacated.
	*out_killed = (int)(enemyKilled - killedBefore);

	*out_dropped = false;
	for (uint i = 0; i < COUNTOF(enemy); ++i)
		if (enemyAvail[i] != 1)
			*out_dropped = true;   // the row is all dead by now, so anything live is a drop
}

/* Armor one pulse belonging to `owner` takes off a lone enemy left tough enough to survive it, which
 * is the drain's own answer rather than the accessor's. Contract in qa.h. */
int qa_chain_pulse_damage(int owner, bool salvo)
{
	const JE_byte tough = 250;   // under the invulnerable sentinel, over anything a pulse deals

	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}

	enemyAvail[0] = 0;
	enemy[0].ex = 100;
	enemy[0].ey = 100;
	enemy[0].armorleft = tough;
	enemy[0].enemytype = 1;

	chain_reset_queue();
	chain_queue_at(enemy[0].ex, enemy[0].ey, owner, salvo, 1);
	chain_reaction_process();

	return tough - enemy[0].armorleft;
}

/* Verify that a salvo tag survives every hop of a long chain. */
bool qa_chain_salvo_latch_holds(int owner)
{
	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}

	const uint fxSaved = chain_fx_enter(owner);
	const int step = endlessPerkChainRadius() / 2;
	endlessSetFxPlayer(fxSaved);

	const int row = 6;
	for (int i = 0; i < row; ++i)
	{
		enemyAvail[i] = 0;
		enemy[i].ex = (JE_integer)(60 + i * step);
		enemy[i].ey = 100;
		enemy[i].armorleft = 1;
		enemy[i].enemytype = 1;
	}

	chain_reset_queue();
	chain_queue_at(enemy[0].ex, enemy[0].ey, owner, true, 1);   // the volley's own pulse

	/* Every hop after the first is queued by the drain, with the window long gone as far as the
	 * live check is concerned; the tag has to come from the parent instead. */
	bool everyHopTagged = true;
	int hops = 0;
	while (chainPulseN > 0 && hops < CHAIN_QUEUE_MAX)
	{
		for (int p = 0; p < chainPulseN; ++p)
			if (!chainPulseSalvo[p])
				everyHopTagged = false;

		chain_reaction_process();
		++hops;
	}

	return everyHopTagged && hops >= 3;
}

/* The cascade, driven through the real queue and the real drain. */
void qa_test_chain_cascade(void)
{
	struct JE_SingleEnemyType savedEnemy[8];
	JE_byte savedAvail[COUNTOF(savedEnemy)];
	memcpy(savedEnemy, enemy, sizeof(savedEnemy));
	memcpy(savedAvail, enemyAvail, sizeof(savedAvail));

	const int radius = endlessPerkChainRadius();
	const int dmg    = endlessPerkChainDamage(false);
	if (radius <= 0 || dmg <= 1)
	{
		qa_check(false, "the chain cascade test needs the perk held by the current effect player");
		return;
	}

	for (int pass = 0; pass <= 1; ++pass)
	{
		const bool touching = (pass == 0);
		const int step = touching ? radius / 2 : radius * 2;

		for (uint i = 0; i < COUNTOF(savedEnemy); ++i)
		{
			memset(&enemy[i], 0, sizeof(enemy[i]));
			enemyAvail[i] = 0;
			enemy[i].ex = (JE_integer)(20 + (int)i * step);
			enemy[i].ey = 100;
			enemy[i].armorleft = (JE_byte)(dmg > 1 ? dmg - 1 : 1);   // one pulse finishes it
			enemy[i].enemytype = 1;
		}

		chain_reset_queue();
		chain_queue_at(enemy[0].ex, enemy[0].ey, 0, false, 1);

		/* One drain is one tick. Run them until the wave dies out, bounded so a queue that refused
		 * to empty fails the tick check below instead of hanging the suite. */
		int ticks = 0;
		while (chainPulseN > 0 && ticks < CHAIN_QUEUE_MAX)
		{
			chain_reaction_process();
			++ticks;
		}

		int dead = 0;
		for (uint i = 0; i < COUNTOF(savedEnemy); ++i)
			if (enemyAvail[i] == 1)
				++dead;

		char label[128];
		if (touching)
		{
			qa_check(dead == (int)COUNTOF(savedEnemy),
			         "a wave carries the whole row when each kill lands inside the next blast");
			/* The row is laid out over several blasts, so no single tick can reach the far end. */
			snprintf(label, sizeof(label), "...one hop per tick, taking %d of them and then settling",
			         ticks);
			qa_check(ticks >= 3 && chainPulseN == 0, label);
		}
		else
		{
			qa_check(dead == 1, "and it stops at the first when the next is out of reach");
		}
	}

	/* Verify scaled boss damage and invulnerable-hull rejection. */
	static const struct { const char *what; JE_byte armor; bool damageable; } hulls[] = {
		{ "a boss the level gave a health bar", 200, true },
		{ "an invulnerable hull",               255, false },
	};
	const JE_byte savedBossLink = boss_bar[0].link_num;
	const JE_byte bossLink = 42;
	boss_bar[0].link_num = bossLink;

	for (uint h = 0; h < COUNTOF(hulls); ++h)
	{
		for (uint i = 0; i < COUNTOF(savedEnemy); ++i)
		{
			memset(&enemy[i], 0, sizeof(enemy[i]));
			enemyAvail[i] = 1;
		}

		enemyAvail[0] = 0;
		enemy[0].ex = 60;
		enemy[0].ey = 100;
		enemy[0].linknum = bossLink;
		enemy[0].armorleft = hulls[h].armor;
		enemy[0].enemytype = 1;

		/* Use separate waves to cross the boss's fractional damage threshold. */
		const int divisor100 = enemy_hp_divisor100(0);
		const int pulses = (divisor100 + ENEMY_DAMAGE_ACCUM_SCALE - 1) / ENEMY_DAMAGE_ACCUM_SCALE + 1;
		for (int t = 0; t < pulses; ++t)
		{
			chain_reset_queue();
			chain_queue_at(enemy[0].ex, enemy[0].ey, 0, false, (JE_word)(t + 1));   // right on it
			chain_reaction_process();
		}

		char label[160];
		snprintf(label, sizeof(label), "%d blasts leave %s at %d armor of %d",
		         pulses, hulls[h].what, enemy[0].armorleft, hulls[h].armor);
		qa_check(enemyAvail[0] == 0
		         && (hulls[h].damageable ? enemy[0].armorleft < hulls[h].armor
		                                 : enemy[0].armorleft == hulls[h].armor), label);
	}

	/* Sustained fire can kill every damageable boss. Linked tiles die and pay out together,
	 * matching a final shot hit. */
	for (uint i = 0; i < COUNTOF(savedEnemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}

	const int bossTiles = 3;
	const int bossWorth = 500;
	for (int i = 0; i < bossTiles; ++i)
	{
		enemyAvail[i] = 0;
		enemy[i].ex = (JE_integer)(60 + i * 8);
		enemy[i].ey = 100;
		enemy[i].linknum = bossLink;
		enemy[i].armorleft = 10;
		enemy[i].enemytype = 1;
		enemy[i].evalue = (Sint16)bossWorth;
	}

	const Sint64 cashBefore = player[0].cash;
	int bossTicks = 0;
	while (bossTicks < 500 && enemyAvail[0] != 1)
	{
		chain_reset_queue();
		chain_queue_at(60, 100, 0, false, (JE_word)(bossTicks + 1));   // one kill's wave per tick
		chain_reaction_process();
		++bossTicks;
	}

	char bossLabel[160];
	snprintf(bossLabel, sizeof(bossLabel), "a boss dies to the blast after %d of them, every tile of it",
	         bossTicks);
	qa_check(enemyAvail[0] == 1 && enemyAvail[1] == 1 && enemyAvail[2] == 1, bossLabel);

	const long bossPaid = (long)(player[0].cash - cashBefore);
	snprintf(bossLabel, sizeof(bossLabel), "...paying %ld for its %d tiles at %d each",
	         bossPaid, bossTiles, bossWorth);
	qa_check(bossPaid == (long)bossTiles * bossWorth, bossLabel);

	player[0].cash = cashBefore;
	boss_bar[0].link_num = savedBossLink;

	/* Verify one hit for a linked hull reached through any tile. */
	for (uint i = 0; i < COUNTOF(savedEnemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}

	const JE_byte tileLink = 7;
	const JE_byte tileArmor = 200;
	const int nearX = 100;
	for (uint i = 0; i < 3; ++i)
	{
		enemyAvail[i] = 0;
		enemy[i].ex = (JE_integer)(nearX + (int)i * (radius / 2));
		enemy[i].ey = 100;
		enemy[i].linknum = tileLink;
		enemy[i].armorleft = tileArmor;
		enemy[i].enemytype = 1;
	}

	chain_reset_queue();
	chain_queue_at(nearX - radius, 100, 0, false, 1);   // reaches tile 0 only
	chain_reaction_process();

	const int worn = (tileArmor - enemy[0].armorleft) + (tileArmor - enemy[1].armorleft)
	               + (tileArmor - enemy[2].armorleft);
	char tileLabel[160];
	snprintf(tileLabel, sizeof(tileLabel),
	         "a three-tile hull clipped at one end takes %d armor, one hit's worth of %d", worn, dmg);
	qa_check(worn == dmg, tileLabel);
	qa_check(enemy[1].armorleft < tileArmor && enemy[0].armorleft == tileArmor,
	         "...and it lands in the middle tile rather than the one the blast touched");
	chain_reset_queue();
	JE_resetSP();   // the drain above threw real rings and bolts
	memcpy(enemy, savedEnemy, sizeof(savedEnemy));
	memcpy(enemyAvail, savedAvail, sizeof(savedAvail));
}

/* Armor a row of pulses takes off a tough hull at `x`, one pulse per entry of `waves`, all queued
 * into one drain. The hull is `tiles` linked tiles (linknum 7), or lone fodder at 1. */
static int qa_chain_pulses_wear(int x, int tiles, const JE_word *waves, int count)
{
	const JE_byte tough = 250;

	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
	for (int i = 0; i < tiles; ++i)
	{
		enemyAvail[i] = 0;
		enemy[i].ex = (JE_integer)(x + i * 8);
		enemy[i].ey = 100;
		enemy[i].armorleft = tough;
		enemy[i].enemytype = 1;
		enemy[i].linknum = (tiles > 1) ? 7 : 0;
	}

	chain_reset_queue();
	for (int p = 0; p < count; ++p)
		chain_queue_at(x, 100, 0, false, waves[p]);
	chain_reaction_process();

	int worn = 0;
	for (int i = 0; i < tiles; ++i)
		worn += tough - enemy[i].armorleft;
	return worn;
}

/* Verify one hit per hull per wave, across drains and overlapping pulses. */
void qa_test_chain_wave_latch(void)
{
	struct JE_SingleEnemyType savedEnemy[8];
	JE_byte savedAvail[COUNTOF(savedEnemy)];
	memcpy(savedEnemy, enemy, sizeof(savedEnemy));
	memcpy(savedAvail, enemyAvail, sizeof(savedAvail));

	const int radius = endlessPerkChainRadius();
	const int dmg    = endlessPerkChainDamage(false);
	if (radius <= 0 || dmg <= 1)
	{
		qa_check(false, "the wave-latch test needs the perk held by the current effect player");
		return;
	}

	static const JE_word oneWave[]  = { 1, 1, 1 };
	static const JE_word twoWaves[] = { 1, 2 };
	char label[160];

	int worn = qa_chain_pulses_wear(100, 1, oneWave, COUNTOF(oneWave));
	snprintf(label, sizeof(label), "three pulses of one wave on a lone hull land one hit, %d of %d",
	         worn, dmg);
	qa_check(worn == dmg, label);

	worn = qa_chain_pulses_wear(100, 1, twoWaves, COUNTOF(twoWaves));
	snprintf(label, sizeof(label), "...and pulses of two waves land twice, %d of %d",
	         worn, 2 * dmg);
	qa_check(worn == 2 * dmg, label);

	worn = qa_chain_pulses_wear(100, 3, oneWave, COUNTOF(oneWave));
	snprintf(label, sizeof(label), "a linked hull under three pulses of one wave takes %d of %d",
	         worn, dmg);
	qa_check(worn == dmg, label);

	worn = qa_chain_pulses_wear(100, 3, twoWaves, COUNTOF(twoWaves));
	snprintf(label, sizeof(label), "...and %d of %d under two waves", worn, 2 * dmg);
	qa_check(worn == 2 * dmg, label);

	/* Across hops: a tough hull inside the seed pulse and inside the pulse of the fodder that seed
	 * kills is hit by the seed and skipped by the hop, then hit again by the next kill's wave. */
	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
	const JE_byte tough = 250;
	enemyAvail[0] = 0;                       // the fodder the seed kills
	enemy[0].ex = 100;
	enemy[0].ey = 100;
	enemy[0].armorleft = 1;
	enemy[0].enemytype = 1;
	enemyAvail[1] = 0;                       // the hull both pulses reach
	enemy[1].ex = (JE_integer)(100 + radius / 2);
	enemy[1].ey = 100;
	enemy[1].armorleft = tough;
	enemy[1].enemytype = 1;

	chain_reset_queue();
	chain_queue_at(100, 100, 0, false, 1);
	int hops = 0;
	while (chainPulseN > 0 && hops < CHAIN_QUEUE_MAX)
	{
		chain_reaction_process();
		++hops;
	}
	snprintf(label, sizeof(label),
	         "the hop a wave's kill queues skips what the wave already hit: %d of %d over %d hops",
	         tough - enemy[1].armorleft, dmg, hops);
	qa_check(enemyAvail[0] == 1 && hops == 2 && tough - enemy[1].armorleft == dmg, label);

	chain_reset_queue();
	chain_queue_at(100, 100, 0, false, 2);   // the next kill, on the same spot
	chain_reaction_process();
	snprintf(label, sizeof(label), "...and the next kill's wave lands on it again, %d of %d",
	         tough - enemy[1].armorleft, 2 * dmg);
	qa_check(tough - enemy[1].armorleft == 2 * dmg, label);

	chain_reset_queue();
	JE_resetSP();
	memcpy(enemy, savedEnemy, sizeof(savedEnemy));
	memcpy(enemyAvail, savedAvail, sizeof(savedAvail));
}

void enemy_death_register_rollback(void)
{
	rollback_register("t2.chainPulse",       chainPulse, sizeof(chainPulse));
	rollback_register("t2.chainPulseN",      &chainPulseN, sizeof(chainPulseN));
	rollback_register("t2.chainPulseLast",   &chainPulseLastLink, sizeof(chainPulseLastLink));
	rollback_register("t2.chainPulseOwner",  chainPulseOwner, sizeof(chainPulseOwner));
	rollback_register("t2.chainPulseSalvo",  chainPulseSalvo, sizeof(chainPulseSalvo));
	rollback_register("t2.chainPulseWave",   chainPulseWave, sizeof(chainPulseWave));
}
