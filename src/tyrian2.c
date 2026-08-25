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
#include "tyrian2.h"

#include "animlib.h"
#include "backgrnd.h"
#include "config.h"
#include "crashlog.h"
#include "custom_episode.h"
#include "custom_weapon.h"
#include "editship.h"
#include "endless.h"
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "fonthand.h"
#include "game_menu.h"
#include "joystick.h"
#include "keyboard.h"
#include "lds_play.h"
#include "loudness.h"
#include "lvllib.h"
#include "menus.h"
#include "mainint.h"
#include "mouse.h"
#include "mtrand.h"
#include "network.h"
#include "net_style.h"
#include "nortsong.h"
#include "nortvars.h"
#include "opentyr.h"
#include "params.h"
#include "pcxload.h"
#include "pcxmast.h"
#include "picload.h"
#include "qa.h"
#include "render_list.h"
#include "rollback.h"
#include "net_rollback.h"
#include "shots.h"
#include "sim_math.h"
#include "sprite.h"
#include "touch_ui.h"
#include "vga256d.h"
#include "video.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Render-list verification harness: replay the captured list each clean frame
// and report any pixels that differ from the real frame. Gated off; kept for debugging.
#define RL_SELFTEST 0

// `outline` draws the frame as a flat silhouette in a dark shade of the colour the art would have
// used, for the "?" pickup's outline pass. Only that pickup passes it, and it always has a bank.
inline static void blit_enemy(SDL_Surface *surface, unsigned int i, signed int x_offset, signed int y_offset, signed int sprite_offset, bool outline);
static void draw_enemy_health_bars(void);
// Defined with the rest of the Random Pickups code (next to JE_makeEnemy), used up in JE_main's
// enemy loop where Super Arcade repaints a dropped ball into its ship's own weapon set.
static bool arcadeSuperPickupRandomActive(void);

// Endless effects on the enemy-shot pool.
// MARTYRDOM and SEEKER ROUNDS both act on enemyShot[] (spawned/moved here in tyrian2.c), so their
// spawn/movement code lives with the pool while the per-modifier decisions live in endless_combat.c.

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

// Center Martyrdom on the visible bounds of a linked or 2x2 body. A single-cell enemy keeps its
// sprite anchor; the freed slot is measured directly before unioning live link partners.
static void endlessMartyrBurstOrigin(unsigned int i, JE_integer *ox, JE_integer *oy)
{
	int left, top, right, bottom;
	enemy_sprite_box(i, &left, &top, &right, &bottom);
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

			// Only the part of the body that is on the playfield counts. A boss parks its anchor
			// above the screen under the group's own linknum, and taking that in would drag the
			// middle out of the visible hull. Same window the enemy visibility test uses.
			if (r <= PLAYFIELD_LEFT || l > PLAYFIELD_RIGHT || b <= 0 || t >= vga_height)
				continue;

			if (l < left)   left = l;
			if (t < top)    top = t;
			if (r > right)  right = r;
			if (b > bottom) bottom = b;
			multiCell = true;
		}
	}

	if (!multiCell)
	{
		*ox = enemy[i].ex + enemy[i].mapoffset;
		*oy = enemy[i].ey;
		return;
	}

	*ox = (JE_integer)((left + right) / 2);
	*oy = (JE_integer)((top + bottom) / 2);
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
static void endlessShockwaveClear(JE_integer sx, JE_integer sy, int radius)
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

/* Derive flip, spotlight, and inverted controls from level state and synchronized Endless
 * modifiers. The result affects simulation and must also run online. */
void JE_deriveStarShowSpecial(void)
{
	starShowVGASpecialCode = smoothies[9-1] + (smoothies[6-1] << 1);

	// Endless replaces a level's scripted spotlight with the zone's seeded roll. Keep flip codes
	// intact, and leave campaign spotlights authored by the level.
	if (endlessMode)
	{
		if (starShowVGASpecialCode == 2)
			starShowVGASpecialCode = 0;
		if (starShowVGASpecialCode == 0 && endlessLightConeActive())
			starShowVGASpecialCode = 2;
	}

	// Topsy Turvy uses the boss screen flip, including its matching control inversion.
	if (endlessFxActive() && (endlessActiveMods & ENDLESS_MOD_TOPSY))
	{
		smoothies[9 - 1] = true;
		starShowVGASpecialCode = 1;
	}
}

// Before damage, the latest armor write becomes full health. After damage, full health can only
// grow. See doc/notes.md#combat.
void enemy_note_full_armor(struct JE_SingleEnemyType *enemy)
{
	if (enemy->armorleft >= 255)
		return;  // the invincible sentinel is not a health value
	if (!enemy->healthbar_seen || enemy->armorleft > enemy->healthbar_max)
		enemy->healthbar_max = enemy->armorleft;
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

static void chain_reset_queue(void)
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

// The divisor a hull spends damage through, in hundredths: the tier multiplier above times the
// endless ordinary-HP overflow, which is the part of the depth curve the 254 armor byte cannot
// hold.
int enemy_hp_divisor100(unsigned int slot)
{
	return enemy_hp_multiplier100(slot) * endlessArmorOverflow100() / ENEMY_DAMAGE_ACCUM_SCALE;
}

// Armor points `damage` buys against this hull, banking the remainder in its accumulator. The player
// shot loop runs the divide itself instead of calling this, because Executioner has to be measured
// across the same one.
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

// What a destroyed enemy leaves behind: the body its death turns into (loot, a rising bomb, a
// second stage, a Super Arcade power-up) and what it was worth, paid to `payee`, a 0-based
// player index.
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
				// Random Pickups: roll the colour instead of walking 1-2-3-4-5. The index is a
				// slot in THIS ship's SAWeapon row, so a roll can only ever hand out one of the
				// five guns it is allowed to fly.
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

			// Endless: a body this death turns into (a rising bomb, a second stage) continues
			// the same enemy, so it takes the tier already decided rather than rolling one and
			// changing colour. Loot is avail 2 and stays untiered.
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

// One tile of a hull that is going down: the flag it sets, what it pays out, its death, and its
// explosion. `staged` asks for the transformation a tile with edlevel -1 owes instead of a death,
// which the caller decides because it depends on how the killing blow matched the group.
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
		// the one helper; never inline any of them here again (see tyrian2.h).
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

// Contract in tyrian2.h. The walk runs on the shared temp2/temp3 on purpose: a further hit of the
// same shot in the same tick reads what they hold, and this keeps the extraction exact.
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
	const int shockRadius = endlessShockwaveRadius(linknum, elite);
	const int martyrShots = endlessMartyrdomBurstShots(linknum, elite);

	// SHOCKWAVE runs BEFORE the martyr burst: on the rare sector carrying both, the sweep clears the
	// fire already in the air and the burst below still gets to spawn. The other order had the sweep
	// silently eat the burst it had just created.
	endlessShockwaveClear(sx, sy, shockRadius);
	if (martyrShots > 0)
	{
		JE_integer burstX, burstY;
		endlessMartyrBurstOrigin(i, &burstX, &burstY);
		endlessSpawnMartyrBurst(burstX, burstY, martyrShots, endlessEliteTint(elite));
	}
	chain_queue_kill(sx, sy, linknum, killer);
}

// Bank 15 is the brightest ramp in every shipped palette, so the pulse reads as a flash on any
// level, and nothing else claims it: elites wear 0xD0, champions 0x50, and 0xD0 is also the
// commonest blast filter in the weapon tables.
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

// Enemies a pulse of `wave` may damage, which is everything carrying hit points that this wave
// has not landed on yet: bosses and elite tiers included, each spending the blast through
// whatever accumulator scales it.
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
static void chain_reaction_process(void)
{
	if (chainPulseN == 0)
	{
		chainPulseLastLink = 0;
		return;
	}
	// Feedback for this drain. Pulses of different waves each still deal their damage; only the
	// flash, the puff and the bolt are held to one per victim, which also keeps a dense frame from
	// spending the whole explosion pool on one enemy.
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
				// Destroyed, and taken the way a killing shot takes it: its payout, its bounty if it
				// carried one, the death effects its tier owes, and the pulse that carries the wave
				// on. A lone enemy goes alone; a linked hull goes whole, boss included.
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

		// Every pulse rings, hit or not: the ring is the only reading of the blast radius the player
		// gets, and one conditional on a hit would arrive after that reading was useful. The cue for
		// landing damage is the part that waits on a hit.
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

/* Contract in qa.h. The row is spaced so clearing it takes several hops, which is the point: only
 * the first pulse is tagged, so every later one has to have inherited the tag rather than read a
 * window that has lapsed. */
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

	/* A boss the level gave a health bar is damageable, so the blast wears it down, spending through
	 * the accumulator that scales it rather than chipping raw armor. An invulnerable hull is not, and
	 * takes nothing however long the blast sits on it. */
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

		/* Its accumulator may swallow several pulses per armor point, so give the blast enough of
		 * them that one point has to land if it lands at all. Each is a fresh kill's wave, since a
		 * wave lands only once. */
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

	/* A multi-tile hull is one target: the blast reaches it if it reaches any tile, and lands once
	 * in the middle rather than once per tile it happens to overlap. Three tiles in a row, with the
	 * pulse placed to clip only the near one. */
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

/* A wave lands on a hull once. Pulses of one wave that overlap on it, in one drain or across the
 * hops that follow, deal one hit between them; a pulse of another wave, which is what the next
 * kill starts, lands again. Contract in qa.h; placed here with the queue and the drain. */
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

// Apply one tier-limited course correction while preserving shot speed. The
// movement loop owns the delay and remaining-pass count.
#define ENDLESS_SEEKER_DELAY_TICKS 17     // ~0.5s at the 35Hz sim between corrections
static void endlessSeekerCorrect(EnemyShotType *s)
{
	float turnCos = 0.0f, turnSin = 0.0f;
	endlessSeekerTurn(&turnCos, &turnSin);
	const float vx = (float)s->sxm, vy = (float)s->sym;
	const float speed = sqrtf(vx * vx + vy * vy);
	if (speed < 0.5f)
		return;                          // a near-stationary shot has no heading to bend
	const Player *const target = &player[endlessDangerTargetPlayer(s->sx, s->sy)];
	const float dx = (float)((int)target->x - s->sx);
	const float dy = (float)((int)target->y - s->sy);
	const float dmag = sqrtf(dx * dx + dy * dy);
	if (dmag < 0.5f)
		return;
	const float ux = dx / dmag, uy = dy / dmag;      // unit vector toward the player
	const float cvx = vx / speed, cvy = vy / speed;  // current unit heading
	const float dot = cvx * ux + cvy * uy;           // cos(angle between heading and target)
	if (dot >= turnCos)
	{
		s->sxm = (JE_integer)roundf(ux * speed);     // within one turn: snap straight at the player, keep the speed
		s->sym = (JE_integer)roundf(uy * speed);
	}
	else
	{
		float sn = turnSin;
		// The cross-product sign selects the shorter rotation.
		if (cvx * uy - cvy * ux < 0.0f)
			sn = -sn;
		s->sxm = (JE_integer)roundf(vx * turnCos - vy * sn);
		s->sym = (JE_integer)roundf(vx * sn + vy * turnCos);
	}
	if (s->sxm == 0 && s->sym == 0)                  // rounding zeroed a tiny vector; keep it moving
	{
		s->sxm = (JE_integer)roundf(vx);
		s->sym = (JE_integer)roundf(vy);
	}
}

boss_bar_t boss_bar[2];

// Link group 0 means both "unlinked enemy" and "unused bar slot". Never treat it as a match.
bool enemy_has_boss_bar(JE_byte linknum)
{
	if (linknum == 0)
		return false;
	for (unsigned int i = 0; i < COUNTOF(boss_bar); i++)
		if (linknum == boss_bar[i].link_num)
			return true;
	return false;
}

// Light this group's health bar for the frames the flash lasts. A hull with no bar has none to light.
void boss_bar_note_hit(JE_byte linknum)
{
	if (linknum == 0)
		return;
	for (unsigned int i = 0; i < COUNTOF(boss_bar); i++)
		if (linknum == boss_bar[i].link_num)
			boss_bar[i].color = 6;
}

/* Level Event Data */
JE_boolean quit, loadLevelOk;

struct JE_EventRecType eventRec[EVENT_MAXIMUM]; /* [1..eventMaximum] */
JE_word levelEnemyMax;
JE_word levelEnemyFrequency;
JE_word levelEnemy[40]; /* [1..40] */

char tempStr[31];

/* Data used for ItemScreen procedure to indicate items available */
JE_byte itemAvail[9][10]; /* [1..9, 1..10] */
JE_byte itemAvailMax[9]; /* [1..9] */

// Render-rate ship movement: the displayed ship extrapolates its last per-tick velocity each
// frame and is reconciled to the 35Hz simulation through the ship override.
static int ship_tick_x[2], ship_tick_y[2];   // ship position captured at the last tick
static int ship_vel_x[2], ship_vel_y[2];       // per-tick movement (cur - prev tick)
static bool ship_pred_have_tick = false;

// Fixed-timestep accumulator: each display frame presents at alpha = accumulator/period, the
// simulation ticks once per full period. Uses the performance counter, not SDL_GetTicks.
static float sim_accumulator = 0.0f;
static Uint64 sim_last_counter = 0;
static Uint64 sim_perf_freq = 0;
static bool sim_timing_init = false;

float debug_interp_alpha = 0.0f;  // last presented interpolation fraction (perf overlay)

// Smoothie levels keep render_gs as persistent background plasma and smoothie_frame as the current
// filtered background. Full-resolution mode also draws foreground into smoothie_frame; mixed and
// Vita cached modes keep the background pristine and use a separate foreground target.
static SDL_Surface *render_gs = NULL;
static SDL_Surface *smoothie_frame = NULL;
static SDL_Surface *smoothie_present_frame = NULL;  // separate foreground target for cached plasma

// (Re)create a lazily-allocated 8-bit surface at scale x the logical size. A factor
// change discards the old content; fine for the plasma base: the contractive filters
// rebuild it from black within a couple of frames (masked by any level fade).
static SDL_Surface *ensure_scaled_surface(SDL_Surface **surf, int scale)
{
	const int w = vga_width * scale, h = vga_height * scale;
	if (*surf != NULL && ((*surf)->w != w || (*surf)->h != h))
	{
		SDL_FreeSurface(*surf);
		*surf = NULL;
	}
	if (*surf == NULL)
		*surf = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
	return *surf;
}

static SDL_Surface *get_render_gs(int scale)
{
	return ensure_scaled_surface(&render_gs, scale);
}

static SDL_Surface *get_smoothie_frame(int scale)
{
	return ensure_scaled_surface(&smoothie_frame, scale);
}

static SDL_Surface *get_smoothie_present_frame(int scale)
{
	return ensure_scaled_surface(&smoothie_present_frame, scale);
}

// Supersampled present path: the interpolated playfield renders NxN into pf_hi, composites into
// vga_hi with the 1x HUD block-expanded on top, then presents via present_hi().
static SDL_Surface *pf_hi = NULL;   // NxN playfield replay target (normal levels)
static SDL_Surface *vga_hi = NULL;  // NxN final frame (playfield composite + HUD)

void tyrian2_deinit(void)
{
	SDL_FreeSurface(render_gs);
	render_gs = NULL;
	SDL_FreeSurface(smoothie_frame);
	smoothie_frame = NULL;
	SDL_FreeSurface(smoothie_present_frame);
	smoothie_present_frame = NULL;
	SDL_FreeSurface(pf_hi);
	pf_hi = NULL;
	SDL_FreeSurface(vga_hi);
	vga_hi = NULL;
}

static bool ensure_hi_buffers(int scale)
{
	return ensure_scaled_surface(&pf_hi, scale) != NULL
	    && ensure_scaled_surface(&vga_hi, scale) != NULL;
}

static void ship_pred_on_tick(void)
{
	const int players = twoPlayerMode ? 2 : 1;
	for (int p = 0; p < players; ++p)
	{
		if (ship_pred_have_tick)
		{
			ship_vel_x[p] = player[p].x - ship_tick_x[p];
			ship_vel_y[p] = player[p].y - ship_tick_y[p];
		}
		ship_tick_x[p] = player[p].x;
		ship_tick_y[p] = player[p].y;
	}
	ship_pred_have_tick = true;
}

static int round_signed(float v)
{
	return (int)(v + (v >= 0.0f ? 0.5f : -0.5f));
}

static void update_ship_override(float alpha)
{
	// Extrapolate actual tick velocity. Rollback peers ease presentation-only corrections; shadows
	// and charge meters share the ship offset, while sidekicks interpolate separately.
	static float  rsm_x[2], rsm_y[2];
	static bool   rsm_valid[2];
	static Uint64 rsm_last_counter;
	float rsm_ease = 1.0f;
	if (nrb_active())
	{
		const Uint64 now = SDL_GetPerformanceCounter();
		if (rsm_last_counter != 0)
		{
			const float dt_ms = (float)(now - rsm_last_counter) * 1000.0f
			                  / (float)SDL_GetPerformanceFrequency();
			// Exponential easing remains frame-rate independent when a frame exceeds the time constant.
			rsm_ease = 1.0f - expf(-dt_ms / 60.0f);
			if (rsm_ease > 1.0f) rsm_ease = 1.0f;
			if (rsm_ease < 0.0f) rsm_ease = 0.0f;
		}
		rsm_last_counter = now;
	}

	const int players = twoPlayerMode ? 2 : 1;
	for (int p = 0; p < players; ++p)
	{
		const bool local  = (uint)(p + 1) == thisPlayerNum;
		const bool docked = (p == 1 && twoPlayerLinked);

		// VT drives the local rollback ship per render frame; docked ships still use the carrier.
		if (nrb_active() && vt_ship_owns() && local && !docked)
			continue;

		// A docked ship shares player 1's presentation offset between ticks.
		if (nrb_active() && docked)
		{
			rl_set_ship_override(p, rl_get_ship_override_dx(0), rl_get_ship_override_dy(0));
			continue;
		}

		int vx = ship_vel_x[p], vy = ship_vel_y[p];
		// A large jump (warp, dragonwing spawn, level start) isn't real velocity;
		// snap rather than fling the ship along a bogus extrapolation.
		if (vx > 40 || vx < -40 || vy > 40 || vy < -40)
		{
			vx = 0;
			vy = 0;
		}

		if (nrb_active() && !local)
		{
			// Clamp remote extrapolation to plausible ship speed so corrections are not amplified.
			const int RVMAX = 6;
			if (vx >  RVMAX) vx =  RVMAX; else if (vx < -RVMAX) vx = -RVMAX;
			if (vy >  RVMAX) vy =  RVMAX; else if (vy < -RVMAX) vy = -RVMAX;

			const float tgt_x = (float)ship_tick_x[p] + vx * alpha;
			const float tgt_y = (float)ship_tick_y[p] + vy * alpha;

			// Ease remote corrections, but snap across jumps larger than any ordinary misprediction.
			if (!rsm_valid[p] ||
			    tgt_x - rsm_x[p] > 100.0f || tgt_x - rsm_x[p] < -100.0f ||
			    tgt_y - rsm_y[p] > 100.0f || tgt_y - rsm_y[p] < -100.0f)
			{
				rsm_x[p] = tgt_x;
				rsm_y[p] = tgt_y;
				rsm_valid[p] = true;
			}
			else
			{
				rsm_x[p] += (tgt_x - rsm_x[p]) * rsm_ease;
				rsm_y[p] += (tgt_y - rsm_y[p]) * rsm_ease;
			}

			rl_set_ship_override(p, rsm_x[p] - (float)ship_tick_x[p],
			                        rsm_y[p] - (float)ship_tick_y[p]);
			continue;
		}
		// Float offset: the replay rounds it at the render scale, so a supersampled
		// ship glides on the sub-pixel grid instead of stepping whole pixels.
		rl_set_ship_override(p, vx * alpha, vy * alpha);
	}
}

// Variable-timestep (VT) player ship: the ship alone is simulated at the render rate with
// real dt while the world stays on the fixed 35Hz tick.
bool vt_ship = true;

#define VT_ACCEL      1.0f  // velocity gained per tick while a direction is held (orig accelXC: +1/tick)
#define VT_DIRECT     1.0f  // immediate px/tick while a direction is held (orig CURRENT_KEY_SPEED; kills momentum lag)
#define VT_FRICTION_X 1.0f  // velocity bled per tick with no x input (orig ~1/tick)
#define VT_FRICTION_Y 0.5f  // orig y friction fires every 2nd tick => half rate
#define VT_VMAX       4.0f  // velocity clamp/axis (orig: vel clamp 4); +VT_DIRECT gives ~5 total like the original
// VT_MOUSE_SENS lives in tyrian2.h: the classic per-tick mouse path shares it

static float vt_x[2], vt_y[2], vt_vx[2], vt_vy[2];
static int vt_wrote_vx[2], vt_wrote_vy[2];  // last velocity VT wrote to player[]
static bool vt_seeded[2] = { false, false };


/* Lockstep does not predict the local sprite because shots, sidekicks, and collisions remain at the
 * simulation position. Lower network_delay instead. */

// Raw (un-inverted) mouse motion accumulated by vt_ship_step since the last twiddle
// read; consumed by vt_ship_twiddle_dir (rationale there).
static float vt_twiddle_mx[2], vt_twiddle_my[2];

bool vt_ship_owns(void)
{
	return vt_ship
	    && smoothMotion                            // user's Graphics-menu toggle
	    && smoothScroll != 0 && frameCountMax > 0  // the render-rate present loop must run
	    // Demos replay fixed-tick input and cannot use render-rate ship movement.
	    && !play_demo && !record_demo
	    && !endLevel;
}

static void vt_seed_player(int p)
{
	vt_x[p] = (float)player[p].x;
	vt_y[p] = (float)player[p].y;
	vt_vx[p] = (float)player[p].x_velocity;
	vt_vy[p] = (float)player[p].y_velocity;
	vt_wrote_vx[p] = player[p].x_velocity;
	vt_wrote_vy[p] = player[p].y_velocity;
	vt_seeded[p] = true;
}

static float vt_friction(float v, float f)
{
	if (v > 0.0f) { v -= f; if (v < 0.0f) v = 0.0f; }
	else if (v < 0.0f) { v += f; if (v > 0.0f) v = 0.0f; }
	return v;
}

static void vt_ship_step_player(int p, float dt)
{
	if (!player[p].is_alive)
	{
		// Reset only this dead ship's offset; the other player may still use VT.
		vt_seeded[p] = false;
		rl_set_ship_override(p, 0.0f, 0.0f);
		return;
	}

	// Reseed after local discontinuities. Netplay seeds once because its simulation position lags VT.
	if (!vt_seeded[p]
	    || (!isNetworkGame
	        && (abs(player[p].x - (int)lrintf(vt_x[p])) > 8
	         || abs(player[p].y - (int)lrintf(vt_y[p])) > 8)))
	{
		vt_seed_player(p);
	}

	// Input.
	// Directional input (keyboard/d-pad/stick) feeds momentum; mouse relative motion
	// applies directly. "Inverted controls" levels (smoothies[8]) flip the Y axis.
	const bool invert_y = smoothies[9 - 1];
	float ix = 0.0f, iy = 0.0f;   // directional input -> momentum
	float mdx = 0.0f, mdy = 0.0f; // mouse relative motion -> direct position

	// Per-player device selection applies only to local two-player. Other modes read any device;
	// 0 = any, 1 = keyboard, 2 = mouse, 3+ = one joystick.
	const JE_byte device = (twoPlayerMode && !galagaMode && !isNetworkGame)
	                     ? inputDevice[p]
	                     : 0;
	const bool use_keyboard = (device == 0 || device == 1);
	const bool use_mouse    = (device == 0 || device == 2);
	const bool use_joystick = (device == 0 || device >= 3);
	const int  joy_first    = (device >= 3) ? device - 3 : 0;
	const int  joy_last     = (device >= 3) ? device - 3 : joysticks - 1;

	if (use_keyboard)
	{
		if (keysactive[keySettings[KEY_SETTING_UP]])    iy -= 1.0f;
		if (keysactive[keySettings[KEY_SETTING_DOWN]])  iy += 1.0f;
		if (keysactive[keySettings[KEY_SETTING_LEFT]])  ix -= 1.0f;
		if (keysactive[keySettings[KEY_SETTING_RIGHT]]) ix += 1.0f;
	}

	if (use_joystick)
	{
		for (int j = joy_first; j <= joy_last && j < joysticks; ++j)
		{
			poll_joystick(j);

			// Menu/pause/change-fire are edge-triggered (action_pressed) and this render-rate
			// poll consumes the edge before tick-rate JE_playerMovement can read it, so catch
			// (latch) them here. Without this the discrete "change fire" is eaten most presses.
			if (joystick[j].action_pressed[4]) ingamemenu_pressed = true;  // "menu"
			if (joystick[j].action_pressed[5]) pause_pressed = true;        // "pause"
			if (joystick[j].action_pressed[1]) changefire_pressed = true;   // "change fire"

			if (joystick[j].analog)
			{
				// Stick deflection -> proportional momentum input (clamped ~[-1,1]).
				float ax = joystick_axis_reduce(j, joystick[j].x) / 32.0f;
				float ay = joystick_axis_reduce(j, joystick[j].y) / 32.0f;
				ix += (ax < -1.0f) ? -1.0f : (ax > 1.0f) ? 1.0f : ax;
				iy += (ay < -1.0f) ? -1.0f : (ay > 1.0f) ? 1.0f : ay;
			}

			// D-pad / digital directions, read in both modes so the d-pad works even when
			// the stick is configured analog. (The combined input is clamped below.)
			if (joystick[j].direction[0]) iy -= 1.0f;  // up
			if (joystick[j].direction[2]) iy += 1.0f;  // down
			if (joystick[j].direction[3]) ix -= 1.0f;  // left
			if (joystick[j].direction[1]) ix += 1.0f;  // right
		}
	}

	if (has_mouse && use_mouse)
	{
		// Float relative motion since our last frame (per-frame sampling would round
		// small/diagonal motion away); returns 0 when relative mode is off.
		float mxr = 0.0f, myr = 0.0f;
		mouseGetRelativeMotionF(&mxr, &myr);
		mdx = mxr * VT_MOUSE_SENS;
		mdy = myr * VT_MOUSE_SENS;

		// Stash the raw mouse direction for the twiddle-code detector, un-inverted;
		// the detector applies the "inverted controls" flip itself, like keyboard.
		vt_twiddle_mx[p] += mxr;
		vt_twiddle_my[p] += myr;
	}

	if (invert_y) { iy = -iy; mdy = -mdy; }

	// Clamp combined directional input (keyboard + analog stick + d-pad) to full
	// deflection so overlapping sources can't push past 1.
	if (ix < -1.0f) ix = -1.0f; else if (ix > 1.0f) ix = 1.0f;
	if (iy < -1.0f) iy = -1.0f; else if (iy > 1.0f) iy = 1.0f;

	// Integrate momentum; dt=1 is one classic tick.
	if (ix != 0.0f)
		vt_vx[p] += ix * VT_ACCEL * dt;
	else
		vt_vx[p] = vt_friction(vt_vx[p], VT_FRICTION_X * dt);

	if (iy != 0.0f)
		vt_vy[p] += iy * VT_ACCEL * dt;
	else
		vt_vy[p] = vt_friction(vt_vy[p], VT_FRICTION_Y * dt);

	if (vt_vx[p] >  VT_VMAX) vt_vx[p] =  VT_VMAX;
	if (vt_vx[p] < -VT_VMAX) vt_vx[p] = -VT_VMAX;
	if (vt_vy[p] >  VT_VMAX) vt_vy[p] =  VT_VMAX;
	if (vt_vy[p] < -VT_VMAX) vt_vy[p] = -VT_VMAX;

	// Combine momentum, immediate directional movement, and mouse/touch delta. Endless movement
	// scaling applies to the complete displacement.
	const float mscale = endlessMoveScale();
	const float step_x = ((vt_vx[p] + ix * VT_DIRECT) * dt + mdx) * mscale;
	const float step_y = ((vt_vy[p] + iy * VT_DIRECT) * dt + mdy) * mscale;

	if (isNetworkGame)
	{
		// Keep an unlagged VT position; player[p].x trails by the network delay and cannot be its base.
		vt_x[p] += step_x;
		vt_y[p] += step_y;

		// Apply Endless gravity before this online branch returns. Folding it into
		// VT motion lets vt_ship_commit_net deliver it exactly once.
		vt_x[p] += endlessGravityDriftX() * dt;
		vt_y[p] += endlessGravityDriftY() * dt;

		// Same playfield bounds the sim enforces (mainint.c); both seats share the floor.
		if (vt_x[p] > PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN) { vt_x[p] = PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN; if (vt_vx[p] > 0) vt_vx[p] = 0; }
		if (vt_x[p] < SHIP_LEFT_MARGIN)                    { vt_x[p] = SHIP_LEFT_MARGIN;                    if (vt_vx[p] < 0) vt_vx[p] = 0; }
		if (vt_y[p] > SHIP_BOTTOM_MARGIN)                  { vt_y[p] = SHIP_BOTTOM_MARGIN;                  if (vt_vy[p] > 0) vt_vy[p] = 0; }
		if (vt_y[p] < SHIP_TOP_MARGIN)                     { vt_y[p] = SHIP_TOP_MARGIN;                     if (vt_vy[p] < 0) vt_vy[p] = 0; }

		// Rollback applies local VT at the next tick, so present the live integrator position.
		if (nrb_active() && (uint)(p + 1) == thisPlayerNum)
		{
			rl_set_ship_override(p, vt_x[p] - (float)ship_tick_x[p], vt_y[p] - (float)ship_tick_y[p]);
			return;
		}

		// Lockstep presents the simulation position so sprite, shots, and hitbox agree.
		return;
	}

	vt_x[p] += step_x;
	vt_y[p] += step_y;

	// Apply render-rate-independent Endless gravity; the bounds below handle every heading.
	vt_x[p] += endlessGravityDriftX() * dt;
	vt_y[p] += endlessGravityDriftY() * dt;

	// Same playfield bounds the sim enforces (mainint.c). Stop velocity at walls
	// so we don't accumulate phantom momentum while held against an edge.
	if (vt_x[p] > PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN) { vt_x[p] = PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN; if (vt_vx[p] > 0) vt_vx[p] = 0; }
	if (vt_x[p] < SHIP_LEFT_MARGIN)                    { vt_x[p] = SHIP_LEFT_MARGIN;                    if (vt_vx[p] < 0) vt_vx[p] = 0; }
	if (vt_y[p] > SHIP_BOTTOM_MARGIN)                  { vt_y[p] = SHIP_BOTTOM_MARGIN;                  if (vt_vy[p] > 0) vt_vy[p] = 0; }
	if (vt_y[p] < SHIP_TOP_MARGIN)                     { vt_y[p] = SHIP_TOP_MARGIN;                     if (vt_vy[p] < 0) vt_vy[p] = 0; }

	// Write back so the next 35Hz tick (collisions, firing, homing) sees the
	// current position/velocity.
	player[p].x = (int)lrintf(vt_x[p]);
	player[p].y = (int)lrintf(vt_y[p]);
	player[p].x_velocity = (int)lrintf(vt_vx[p]);
	player[p].y_velocity = (int)lrintf(vt_vy[p]);
	vt_wrote_vx[p] = player[p].x_velocity;
	vt_wrote_vy[p] = player[p].y_velocity;

	// Display the recorded (tick-time) ship sprite at its new continuous position via the override
	// channel the interpolator already understands.
	rl_set_ship_override(p, vt_x[p] - (float)ship_tick_x[p], vt_y[p] - (float)ship_tick_y[p]);
}

// Netplay: publish VT's current (unlagged) position into the ship, so the netcode picks it up
// as this tick's absolute.
void vt_ship_commit_net(int player_index)
{
	if (!vt_ship_owns() || !isNetworkGame)
		return;

	const int p = (player_index <= 0) ? 0 : 1;
	if (!player[p].is_alive || !vt_seeded[p])
		return;  // nothing meaningful to hand over; the simulation keeps the ship where it is

	// Hand the simulation VT's current position. The netcode reads it straight back out as
	// this tick's absolute, sends it, restores the ship, and both machines apply it together
	// network_delay ticks later.
	player[p].x = (int)lrintf(vt_x[p]);
	player[p].y = (int)lrintf(vt_y[p]);

	// The wire carries position. Local tilt and banking still need the matching velocity.
	player[p].x_velocity = (int)lrintf(vt_vx[p]);
	player[p].y_velocity = (int)lrintf(vt_vy[p]);
	vt_wrote_vx[p] = player[p].x_velocity;
	vt_wrote_vy[p] = player[p].y_velocity;
}

// The other player in a network game: its ship is placed from the deltas arriving on the wire,
// so VT must not integrate input for it; that input belongs to the machine it is sitting at.
static bool vt_player_is_net_remote(int p)
{
	return isNetworkGame && (uint)(p + 1) != thisPlayerNum;
}

// True while the sim, not VT, owns this ship's position.  Player 2 docked to player 1 as the
// Dragonwing is placed from player 1's position every tick (mainint.c), so VT must keep its
// hands off until it undocks; at which point the seed-on-large-jump check picks it back up.
static bool vt_player_is_sim_driven(int p)
{
	return vt_player_is_net_remote(p) || (p == 1 && twoPlayerLinked);
}

void vt_ship_step(float dt)  // dt = this frame's fraction of a 35Hz tick
{
	if (dt <= 0.0f)
		return;

	const int players = twoPlayerMode ? 2 : 1;
	for (int p = 0; p < players; ++p)
	{
		if (vt_player_is_sim_driven(p))
		{
			vt_seeded[p] = false;

			// In netplay VT owns no ship's rendering at all; update_ship_override() runs
			// immediately after this and drives both ships. Leave the channel unchanged.
			if (!isNetworkGame)
			{
				if (p == 1 && twoPlayerLinked)
				{
					// A docked Dragonwing shares player 1's current sub-tick offset.
					rl_set_ship_override(p, rl_get_ship_override_dx(0), rl_get_ship_override_dy(0));
				}
				else
				{
					rl_set_ship_override(p, 0.0f, 0.0f);
				}
			}
			continue;
		}

		vt_ship_step_player(p, dt);
	}
}

// Refresh trailing-sidekick history when VT, rather than the sim tick, moves the ship.
static int hist_prev_x[2] = { -32768, -32768 }, hist_prev_y[2] = { -32768, -32768 };

static void vt_refresh_position_history(int p)
{
	if (player[p].x == hist_prev_x[p] && player[p].y == hist_prev_y[p])
		return;
	hist_prev_x[p] = player[p].x;
	hist_prev_y[p] = player[p].y;

	for (unsigned int i = 1; i < COUNTOF(player[p].old_x); ++i)
	{
		player[p].old_x[i - 1] = player[p].old_x[i];
		player[p].old_y[i - 1] = player[p].old_y[i];
	}
	player[p].old_x[COUNTOF(player[p].old_x) - 1] = player[p].x;
	player[p].old_y[COUNTOF(player[p].old_x) - 1] = player[p].y;
}

static void vt_ship_tick_player(int p)
{
	if (!vt_seeded[p] || !player[p].is_alive)
		return;

	// Hard reposition (respawn/warp/link): re-seed instead of folding.  Skipped in netplay
	// for the same reason as the seed check in vt_ship_step_player: vt_x is not the ship's
	// position there, so this would fire constantly.
	if (!isNetworkGame
	    && (abs(player[p].x - (int)lrintf(vt_x[p])) > 8
	     || abs(player[p].y - (int)lrintf(vt_y[p])) > 8))
	{
		vt_seed_player(p);
		return;
	}

	// Fold external velocity impulses (magnet fields, knockback) into VT: they modify
	// player.x_velocity during the tick and JE_playerMovement's integration is skipped.
	vt_vx[p] += (float)(player[p].x_velocity - vt_wrote_vx[p]);
	vt_vy[p] += (float)(player[p].y_velocity - vt_wrote_vy[p]);

	// Rollback applies positions mid-tick, where the normal history update already runs.
	if (!nrb_active())
		vt_refresh_position_history(p);
}

void vt_ship_tick(void)  // once per 35Hz tick, before ship_pred_on_tick()
{
	if (!vt_ship_owns())
		return;

	const int players = twoPlayerMode ? 2 : 1;
	for (int p = 0; p < players; ++p)
	{
		if (vt_player_is_net_remote(p))
		{
			// Maintain remote trailing-sidekick history unless rollback or docking already did so.
			if (!nrb_active() && !(p == 1 && twoPlayerLinked))
				vt_refresh_position_history(p);
			continue;
		}

		if (vt_player_is_sim_driven(p))
			continue;

		vt_ship_tick_player(p);
	}
}

// Per-tick ship movement for shots that track the ship (delta_x_shot_move, e.g. the laser).
void vt_ship_shot_delta(int player_index, int *out_dx, int *out_dy)
{
	const int p = (player_index <= 0) ? 0 : 1;
	if (!ship_pred_have_tick)
	{
		*out_dx = 0;
		*out_dy = 0;
		return;
	}
	*out_dx = player[p].x - ship_tick_x[p];
	*out_dy = player[p].y - ship_tick_y[p];
}

// Hand the twiddle-code detector the mouse direction since the last call as a -1/0/+1 per axis
// (raw / un-inverted), then reset the accumulator.
void vt_ship_twiddle_dir(int player_index, int *out_dx, int *out_dy)
{
	const int p = (player_index <= 0) ? 0 : 1;
	const float deadzone = 1.0f;  // screen px of motion before a direction counts

	const float ax = vt_twiddle_mx[p], ay = vt_twiddle_my[p];
	vt_twiddle_mx[p] = 0.0f;
	vt_twiddle_my[p] = 0.0f;

	*out_dx = 0;
	*out_dy = 0;

	// Twiddle codes are cardinal sequences, so mirror SF_twiddleTarget's cone. It is measured
	// on the raw accumulator: quantizing first would leave every diagonal at one against one,
	// which keeps both axes whatever the lean was. Movement itself stays in vt_ship_step.
	if (ax > deadzone)       *out_dx =  1;
	else if (ax < -deadzone) *out_dx = -1;
	if (ay > deadzone)       *out_dy =  1;
	else if (ay < -deadzone) *out_dy = -1;

	if (*out_dx != 0 && *out_dy != 0)
	{
		if (fabsf(ax) > 2.0f * fabsf(ay))
			*out_dy = 0;
		else if (fabsf(ay) > 2.0f * fabsf(ax))
			*out_dx = 0;
	}
}

/* The spotlight sits where the ship this machine flies is drawn this frame: the tick position
 * plus the override channel on an interpolated present, the sim position on the tick present.
 * The peer's ship stays dark here and lights its own screen. Presentation only. */
static void spotlight_anchor(bool interpolate, float *out_x, float *out_y)
{
	const int p = (isNetworkGame && thisPlayerNum >= 2) ? 1 : 0;
	if (interpolate)
	{
		*out_x = (float)ship_tick_x[p] + rl_get_ship_override_dx(p);
		*out_y = (float)ship_tick_y[p] + rl_get_ship_override_dy(p);
	}
	else
	{
		*out_x = (float)player[p].x;
		*out_y = (float)player[p].y;
	}
}

// Copy the freshly-drawn playfield into VGAScreenSeg, applying the special
// vertical-flip / lighting composites when active; interpolated in-between frames
// re-composite through here too.
static void composite_playfield(SDL_Surface *playfield, bool interpolate)
{
	JE_byte *src;
	Uint8 *s = VGAScreenSeg->pixels;

	int x, y, lightx, lighty, lightdist;

	src = playfield->pixels;
	src += PLAYFIELD_LEFT;  // crop off the off-screen entry margin; see video.h

	if (starShowVGASpecialCode == 1)
	{
		src += playfield->pitch * 183;
		for (y = 0; y < 184; y++)
		{
			memmove(s, src, PLAYFIELD_WIDTH);
			s += VGAScreenSeg->pitch;
			src -= playfield->pitch;
		}
	}
	else if (starShowVGASpecialCode == 2 && processorType >= 2)
	{
		float litx, lity;
		spotlight_anchor(interpolate, &litx, &lity);
		lighty = 172 - round_signed(lity);
		lightx = (PLAYFIELD_WIDTH - PLAYFIELD_X_SHIFT + 5) - round_signed(litx);

		for (y = 184; y; y--)
		{
			if (lighty > y)
			{
				for (x = PLAYFIELD_WIDTH; x; x--)
				{
					*s = (*src & 0xf0) | ((*src >> 2) & 0x03);
					s++;
					src++;
				}
			}
			else
			{
				for (x = PLAYFIELD_WIDTH; x; x--)
				{
					lightdist = abs(lightx - x) + lighty;
					if (lightdist < y)
						*s = *src;
					else if (lightdist - y <= 5)
						*s = (*src & 0xf0) | (((*src & 0x0f) + (3 * (5 - (lightdist - y)))) / 4);
					else
						*s = (*src & 0xf0) | ((*src & 0x0f) >> 2);
					s++;
					src++;
				}
			}
			s += VGAScreenSeg->pitch - PLAYFIELD_WIDTH;
			src += playfield->pitch - PLAYFIELD_WIDTH;
		}
	}
	else
	{
		for (y = 0; y < 184; y++)
		{
			memmove(s, src, PLAYFIELD_WIDTH);
			s += VGAScreenSeg->pitch;
			src += playfield->pitch;
		}
	}
}

// composite_playfield at NxN: same three modes (normal copy, vertical flip, spotlight) on the
// supersampled playfield, writing into vga_hi's playfield region.
static void composite_playfield_hi(SDL_Surface *playfield, SDL_Surface *out, int scale,
                                   bool interpolate)
{
	const int width = PLAYFIELD_WIDTH * scale;
	const int rows = 184 * scale;
	const JE_byte *src = (const JE_byte *)playfield->pixels + PLAYFIELD_LEFT * scale;
	Uint8 *s = (Uint8 *)out->pixels;

	if (starShowVGASpecialCode == 1)
	{
		src += (size_t)playfield->pitch * (rows - 1);
		for (int y = 0; y < rows; y++)
		{
			memmove(s, src, width);
			s += out->pitch;
			src -= playfield->pitch;
		}
	}
	else if (starShowVGASpecialCode == 2 && processorType >= 2)
	{
		// Scaled before rounding, so under supersampling the cone glides on the sub-pixel grid
		// with the ship.
		float litx, lity;
		spotlight_anchor(interpolate, &litx, &lity);
		const float edge = (float)(PLAYFIELD_WIDTH - PLAYFIELD_X_SHIFT + 5);
		const int lighty = round_signed((172.0f - lity) * (float)scale);
		const int lightx = round_signed((edge - litx) * (float)scale);
		const int band = 5 * scale;

		for (int y = rows; y; y--)
		{
			if (lighty > y)
			{
				for (int x = width; x; x--)
				{
					*s = (*src & 0xf0) | ((*src >> 2) & 0x03);
					s++;
					src++;
				}
			}
			else
			{
				for (int x = width; x; x--)
				{
					const int lightdist = abs(lightx - x) + lighty;
					if (lightdist < y)
						*s = *src;
					else if (lightdist - y <= band)
						*s = (*src & 0xf0) | (((*src & 0x0f) + (3 * (band - (lightdist - y))) / scale) / 4);
					else
						*s = (*src & 0xf0) | ((*src & 0x0f) >> 2);
					s++;
					src++;
				}
			}
			s += out->pitch - width;
			src += playfield->pitch - width;
		}
	}
	else
	{
		for (int y = 0; y < rows; y++)
		{
			memmove(s, src, width);
			s += out->pitch;
			src += playfield->pitch;
		}
	}
}

// Block-expand the 1x HUD onto the hi frame: the right-hand HUD column for the
// playfield rows, and the full width below the playfield. The HUD is tick-drawn
// 1x art (plus the per-frame power gauge), so expanding is exact; it looks
// identical to the classic path.
static void expand_hud_to_hi(SDL_Surface *src, SDL_Surface *hi, int scale)
{
	for (int y = 0; y < vga_height; ++y)
	{
		const int x_start = (y < 184) ? PLAYFIELD_WIDTH : 0;
		const Uint8 *sp = (const Uint8 *)src->pixels + y * src->pitch + x_start;
		Uint8 *const d0 = (Uint8 *)hi->pixels + (y * scale) * hi->pitch + x_start * scale;

		Uint8 *d = d0;
		for (int x = x_start; x < vga_width; ++x, ++sp, d += scale)
			memset(d, *sp, scale);

		const int row_bytes = (vga_width - x_start) * scale;
		for (int k = 1; k < scale; ++k)
			memcpy(d0 + k * hi->pitch, d0, row_bytes);
	}
}

// Block-expand a smaller 8-bit frame onto the hi frame. With Sub-pixel FX off the smoothie plasma
// is filtered at native size and expanded here, leaving entities at the sub-pixel factor. The shop
// preview expands its menu frame the same way.
void expand_frame_to_hi(SDL_Surface *src, SDL_Surface *hi, int scale)
{
	assert(scale >= 1 && hi->w >= src->w * scale && hi->h >= src->h * scale);

	for (int y = 0; y < src->h; ++y)
	{
		const Uint8 *sp = (const Uint8 *)src->pixels + y * src->pitch;
		Uint8 *const d0 = (Uint8 *)hi->pixels + (y * scale) * hi->pitch;

		Uint8 *d = d0;
		switch (scale)
		{
		case 1:
			memcpy(d, sp, (size_t)src->w);
			break;
		case 2:
			for (int x = 0; x < src->w; ++x)
			{
				const Uint8 v = *sp++;
				*d++ = v; *d++ = v;
			}
			break;
		case 3:
			for (int x = 0; x < src->w; ++x)
			{
				const Uint8 v = *sp++;
				*d++ = v; *d++ = v; *d++ = v;
			}
			break;
		case 4:
			for (int x = 0; x < src->w; ++x)
			{
				const Uint8 v = *sp++;
				*d++ = v; *d++ = v; *d++ = v; *d++ = v;
			}
			break;
		case 5:
			for (int x = 0; x < src->w; ++x)
			{
				const Uint8 v = *sp++;
				*d++ = v; *d++ = v; *d++ = v; *d++ = v; *d++ = v;
			}
			break;
		default:
			for (int x = 0; x < src->w; ++x)
			{
				const Uint8 v = *sp++;
				for (int k = 0; k < scale; ++k)
					*d++ = v;
			}
			break;
		}

		const int row_bytes = src->w * scale;
		for (int k = 1; k < scale; ++k)
			memcpy(d0 + k * hi->pitch, d0, row_bytes);
	}
}

// Soul of Zinglon light pillar, drawn at display rate from the per-tick request (zinglonPillar*).
// cx is in HI units; temp is the 1x half-width.
static void draw_zinglon_pillar(SDL_Surface *surface, int cx, int temp, int scale)
{
	const int bottom = 184 * scale;
	int x0 = cx - temp * scale;
	int x1 = cx + temp * scale + (scale - 1);
	if (x0 < 0) x0 = 0;
	JE_barBright(surface, x0, 0, x1, bottom);
	x0 = cx - (temp + 2) * scale;
	x1 = cx + (temp + 2) * scale + (scale - 1);
	if (x0 < 0) x0 = 0;
	JE_barBright(surface, x0, 0, x1, bottom);
}

/* Both ships off the same per-tick request. */
static void draw_active_zinglon_pillars(SDL_Surface *surface, int scale, bool interpolate)
{
	for (uint p = 0; p < COUNTOF(zinglonPillarActive); ++p)
	{
		if (!zinglonPillarActive[p])
			continue;
		const float offset = interpolate ? rl_get_ship_override_dx(p) : 0.0f;
		const int cx = round_signed(((float)zinglonPillarCX[p] + offset) * scale);
		draw_zinglon_pillar(surface, cx, zinglonPillarTemp[p], scale);
	}
}

// Last PRESENTED twoPlayerLinked value, for the fuse/unfuse sound cue edge detector
// in the level loop.  Presentation state: unregistered, reset at level start.
static bool link_cue_state = false;

// Set once everything is released after a fatal hit, arming the endless death menu's "any input
// but Esc cuts the wreck animation short" test so it can't fire on input that was already held.
// Input state, not sim state: unregistered, reset at level start (as link_cue_state above).
static bool deathSkipArmed = false;

// Ticks the endless death menu has let GAME OVER stand before opening itself. Reset at level
// start alongside deathSkipArmed.
static uint deathGameOverTicks = 0;

// The beat runs as long as the game-over song does, within these bounds (sim ticks, ~35/s at
// normal speed): the floor covers a jingle that never plays, music-off, or audio-disabled run
// that would otherwise finish on its first tick; the cap covers a song that never ends.
#define DEATH_GAMEOVER_TICKS_MIN 70   // ~2s
#define DEATH_GAMEOVER_TICKS_MAX 700  // ~20s

// Generator power bar render state: a HUD overlay on VGAScreenSeg, redrawn every presented
// frame at an interpolated level with a sub-pixel anti-aliased top edge.
static bool power_gauge_active = false;
static int power_render_prev = 0, power_render_cur = 0;  // `power` (0..900) at the prev/cur tick
static int salvo_render_prev = 0, salvo_render_cur = 0;  // ...and the salvo green share (0..100)

// The Opening Salvo green share (0..100) for the ship this machine flies. The perk is personal, so
// the gauge names its ship rather than trusting the fx context, which the last simulated ship
// leaves pointing at itself -- in co-op that is not necessarily ours.
static int local_salvo_gauge_percent(void)
{
	const uint fxSaved = endlessFxPlayer();
	endlessSetFxPlayer(gameplay_local_player_index());
	const int pct = endlessOpeningSalvoGaugePercent();
	endlessSetFxPlayer(fxSaved);
	return pct;
}

// Geometry shared by the generator and the arcade lives bar in the same HUD slot.
enum { PG_Y_BOTTOM = 104, PG_BAR_MAX = 93, PG_BASE = 113, PG_POWER_MAX = 900, PG_SEG_SHADE_MAX = 13 };

// A gauge rect given in 1x HUD coordinates, painted at the present pass's supersample factor.
static void gauge_bar_fill(SDL_Surface *dst, int scale, int x1, int y1, int x2, int y2, Uint8 color)
{
	fill_rectangle_xy(dst, x1 * scale, y1 * scale, (x2 + 1) * scale - 1, (y2 + 1) * scale - 1, color);
}

// The one row the bar's top edge only partly covers, blended from the bank floor up to the shade
// the bar ends on. Below 0.04 the row would only carry the floor colour, which reads as a stray dot.
static void gauge_bar_edge_row(SDL_Surface *dst, int x0, int x1, int top, float cover, int shade, int dark)
{
	if (cover <= 0.04f || top < 1)
		return;

	int edgeCol = dark + (int)(cover * (shade - dark) + 0.5f);
	if (edgeCol > shade)
		edgeCol = shade;
	fill_rectangle_xy(dst, x0, top - 1, x1, top - 1, (Uint8)edgeCol);
}

// level is the pixel height with a fractional top edge. salvo_frac tints that share green;
// segments divides counted resources into visible blocks.
static void draw_gauge_bar(SDL_Surface *dst, int scale, float level, float salvo_frac, int segments)
{
	// 9 pixels wide (x1..x2). The classic art drew this gauge 1px narrower than the
	// shield/armor bars; extend it right by one so all three gauges match at 9px.
	const int x1 = HUD_X(269), x2 = HUD_X(277);

	if (level < 0.0f)
		level = 0.0f;
	else if (level > PG_BAR_MAX)
		level = PG_BAR_MAX;

	const int full = (int)level;          // solid whole rows
	const int dir = gaugeGradGenerator;   // GaugeGradientDir

	// Exact top edge in target rows, and how much of the row above it reaches into. At scale 1
	// the solid fill stops at the whole row and `cover` is the classic anti-aliased remainder;
	// above that, the sub-rows between are filled solid and only the last one is blended.
	const float topf = (float)((PG_Y_BOTTOM + 1) * scale) - level * (float)scale;
	const int top = (int)ceilf(topf);
	const float cover = (float)top - topf;
	const int solidTop = (PG_Y_BOTTOM - full + 1) * scale;  // first row of the last whole row

	// Kill-fire BOON window: main-gun fire is power-free, so recolour the gauge under the same
	// condition that gates the free power. The gauge is the local ship's, and the context goes back
	// afterwards: this also runs at render rate, and the sim must not inherit a HUD's fx ship.
	const uint fxSaved = endlessFxPlayer();
	endlessSetFxPlayer(gameplay_local_player_index());
	int base = PG_BASE;
	if (endlessFxActive() && endlessTurbodriveActive() && !endlessKillFireIsEvil())
		base = ENDLESS_FREE_POWER_GAUGE_BASE;
	endlessSetFxPlayer(fxSaved);

	// Opening Salvo paints from the bottom and takes precedence over the kill-fire tint.
	int salvoRows = (salvo_frac > 0.0f) ? (int)(full * salvo_frac + 0.5f) : 0;
	if (salvoRows > full)
		salvoRows = full;
	const int salvoBase = ENDLESS_SALVO_GAUGE_BASE;
	// The AA row sits one above the solid bar, so it only reads green on a bar that is fully green.
	const int edgeBase = (salvoRows > 0 && salvoRows >= full) ? salvoBase : base;
	const int edgeDark = edgeBase & ~0x0F;  // bank floor: the AA top edge blends up from here

	// Clear the bar slot (its background is black, like the original shrink fill).
	gauge_bar_fill(dst, scale, x1, PG_Y_BOTTOM - PG_BAR_MAX, x2, PG_Y_BOTTOM, 0);

	if (dir == GAUGE_GRAD_LEFT || dir == GAUGE_GRAD_RIGHT)
	{
		// Horizontal gradient: each column is a fixed shade stepping across the width,
		// with the sub-pixel top edge applied per column. Lifted +2 shades to match the
		// slightly-brighter horizontal ramp on the shield/armor bars (in-family).
		for (int j = 0; j <= x2 - x1; j++)
		{
			const int off = (dir == GAUGE_GRAD_RIGHT) ? j : (x2 - x1 - j);
			const int cx0 = (x1 + j) * scale, cx1 = (x1 + j + 1) * scale - 1;
			if (salvoRows >= 1)
				gauge_bar_fill(dst, scale, x1 + j, PG_Y_BOTTOM - salvoRows + 1, x1 + j, PG_Y_BOTTOM, (Uint8)(salvoBase + 2 + off));
			if (full > salvoRows)
				gauge_bar_fill(dst, scale, x1 + j, PG_Y_BOTTOM - full + 1, x1 + j, PG_Y_BOTTOM - salvoRows, (Uint8)(base + 2 + off));
			if (full < PG_BAR_MAX)
			{
				const int shade = edgeBase + 2 + off;
				if (top < solidTop)
					fill_rectangle_xy(dst, cx0, top, cx1, solidTop - 1, (Uint8)shade);
				gauge_bar_edge_row(dst, cx0, cx1, top, cover, shade, edgeDark);
			}
		}

		// Separate filled segments with one blank row without disturbing the bar's top edge.
		for (int i = 1; i < segments; ++i)
		{
			const int h = PG_BAR_MAX * i / segments;
			if (h >= 1 && h < full)
				gauge_bar_fill(dst, scale, x1, PG_Y_BOTTOM - h + 1, x2, PG_Y_BOTTOM - h + 1, 0);
		}
	}
	else if (segments > 1)
	{
		// Give each vertical segment a fixed bank shade and reserve its top row as a separator.
		for (int i = 1; i <= segments; ++i)
		{
			const int lo = PG_BAR_MAX * (i - 1) / segments;  // boundary under this block
			const int hi = PG_BAR_MAX * i / segments;        // boundary at its top

			if (lo + 1 > full)
				break;

			int blockTop = (i < segments) ? hi - 1 : hi;  // topmost block has no gap above it
			if (blockTop > full)
				blockTop = full;
			if (blockTop < lo + 1)
				continue;

			const int step = (dir == GAUGE_GRAD_DOWN) ? (segments - i) : (i - 1);
			const int shade = step * PG_SEG_SHADE_MAX / (segments - 1);
			gauge_bar_fill(dst, scale, x1, PG_Y_BOTTOM - blockTop + 1, x2, PG_Y_BOTTOM - lo, (Uint8)(base + shade));
		}
	}
	else
	{
		// Vertical gradient, drawn bottom-up in same-shade bands. Up = classic (shade
		// PG_BASE + h/7, darkest at the bottom); Down mirrors the gradient within the fill.
		// A band also ends at the salvo boundary, since the two sides use different banks.
		for (int h = 1; h <= full; )
		{
			const int shade = (dir == GAUGE_GRAD_DOWN) ? (full - h) / 7 : h / 7;
			const int rowBase = (h <= salvoRows) ? salvoBase : base;
			int h2 = h;
			while (h2 + 1 <= full &&
			       ((dir == GAUGE_GRAD_DOWN) ? (full - (h2 + 1)) / 7 : (h2 + 1) / 7) == shade &&
			       (((h2 + 1) <= salvoRows) ? salvoBase : base) == rowBase)
				++h2;
			gauge_bar_fill(dst, scale, x1, PG_Y_BOTTOM - h2 + 1, x2, PG_Y_BOTTOM - h + 1, (Uint8)(rowBase + shade));
			h = h2 + 1;
		}

		// The leading edge above the last whole row: solid for the sub-rows it fully covers, then one
		// row dimmed toward the darkest shade by what is left, so the top edge moves at sub-pixel
		// resolution as the bar fills.
		if (full < PG_BAR_MAX)
		{
			const int barCol = (dir == GAUGE_GRAD_DOWN) ? edgeBase : (edgeBase + full / 7);
			if (top < solidTop)
				fill_rectangle_xy(dst, x1 * scale, top, (x2 + 1) * scale - 1, solidTop - 1, (Uint8)barCol);
			gauge_bar_edge_row(dst, x1 * scale, (x2 + 1) * scale - 1, top, cover, barCol, edgeDark);
		}
	}
}

// Generator power (0..PG_POWER_MAX) -> a smooth, un-segmented bar.
static void draw_power_gauge(SDL_Surface *dst, int scale, float power_value, float salvo_frac)
{
	draw_gauge_bar(dst, scale, power_value * PG_BAR_MAX / (float)PG_POWER_MAX, salvo_frac, 0);
}

// The arcade modes have no generator, so its gauge slot sits empty; hand it to the life counter
// instead: one segment per life, full bar at ARCADE_LIVES_MAX.
static void draw_lives_gauge(int lives)
{
	if (lives < 0)
		lives = 0;
	else if (lives > ARCADE_LIVES_MAX)
		lives = ARCADE_LIVES_MAX;

	draw_gauge_bar(VGAScreenSeg, 1, (float)(PG_BAR_MAX * lives / ARCADE_LIVES_MAX), 0.0f, ARCADE_LIVES_MAX);
}

static void draw_boss_bar_present(SDL_Surface *dst, int scale, float alpha);

void JE_starShowVGA(void)
{
	if (qa_fast_forward)
	{
		quitRequested = false;
		skipStarShowVGA = false;
		return;
	}

	if (!playerEndLevel && !skipStarShowVGA)
	{
		// Zinglon pillar at the tick position: baseline for the non-interpolated
		// present paths; the interp loop below redraws it shifted after replay.
		draw_active_zinglon_pillars(game_screen, 1, false);

		composite_playfield(game_screen, false);

		if (smoothScroll != 0)
		{
			// Smoothie levels stage backgrounds and foregrounds; normal levels use game_screen.
			const bool can_interp = frameCountMax > 0 && smoothMotion;

			// Supersample factor for this present pass (Auto follows the scaler; see
			// video.h). The hi path needs its buffers; on any allocation failure it
			// degrades to the classic 1x path; never to a missing frame.
			int rss = can_interp ? effective_supersample() : 1;
			const bool use_hi = rss > 1 && ensure_hi_buffers(rss);
			if (!use_hi)
				rss = 1;

			// The smoothie feedback filter covers the whole playfield, so its cost
			// scales with the square of the factor. Sub-pixel FX off runs it at
			// native size and expands the result into the hi frame. Only the layers
			// the filter consumes drop with it; the rest stay sub-pixel (split_bg).
			const int pss = (anySmoothies && !smoothie_full_res) ? 1 : rss;
			const bool split_bg = (pss != rss);
			// Vita cannot lower the spatial factor below 1x. Its low-cost mode instead computes the
			// current tick's plasma once and reuses it while foreground-local movement stays smooth.
#ifdef __vita__
			const bool tick_plasma = anySmoothies && !smoothie_full_res && rss == 1;
#else
			const bool tick_plasma = false;
#endif

			SDL_Surface *const plasma_buf  = anySmoothies ? get_smoothie_frame(pss) : NULL;
			SDL_Surface *const interp_buf  = anySmoothies ? (tick_plasma ? get_smoothie_present_frame(rss)
			                                                        : (pss == rss ? plasma_buf : pf_hi))
			                               : (use_hi ? pf_hi : game_screen);
			SDL_Surface *const bg_feedback = anySmoothies ? get_render_gs(pss) : NULL;
			bool tick_plasma_ready = false;

			if (can_interp && interp_buf != NULL
			    && (!anySmoothies || (plasma_buf != NULL && bg_feedback != NULL)))
			{
				// Present every display frame at alpha = accumulator/period (real
				// elapsed time); break to run the next sim tick once a full period has
				// elapsed. Leftover time carries over, keeping the sim rate exact.
				const float period = (float)frameCountMax * get_delay_period();

				if (!sim_timing_init)
				{
					sim_perf_freq = SDL_GetPerformanceFrequency();
					sim_last_counter = SDL_GetPerformanceCounter();
					sim_accumulator = 0.0f;
					sim_timing_init = true;
				}

				const float counter_to_ms = 1000.0f / (float)sim_perf_freq;

				if (tick_plasma)
				{
					memcpy(plasma_buf->pixels, bg_feedback->pixels,
					       (size_t)bg_feedback->h * bg_feedback->pitch);
					rl_replay_bg(plasma_buf, 1.0f, pss, false);
					tick_plasma_ready = true;
				}

				for (;;)
				{
					const Uint64 now = SDL_GetPerformanceCounter();
					float elapsed = (float)(now - sim_last_counter) * counter_to_ms;
					sim_last_counter = now;
					if (elapsed > period * 4.0f)
						elapsed = period;  // spiral guard (lag spike / resume from pause)
					sim_accumulator += elapsed;

					// Advance the VT ship by the REAL elapsed time before the break check:
					// stepping only on rendered frames discards the elapsed time of the
					// iteration that triggers a sim tick, which reads as visible stutter
					// even at a solid 60 fps.
					const bool vt_owns = vt_ship_owns();
					if (vt_owns)
						vt_ship_step(elapsed / period);

					if (sim_accumulator >= period)
					{
						sim_accumulator -= period;
						if (sim_accumulator > period)
							sim_accumulator = period;  // clamp backlog: at most one tick behind
						break;  // time for the next simulation tick
					}

					const float alpha = sim_accumulator / period;
					debug_interp_alpha = alpha;  // expose for the perf overlay
					// Extrapolate the ship for this frame whenever VT is not the thing moving
					// it. In netplay VT owns the *input* but never the position; the wire
					// does. Both ships still need interpolation here or they would
					// step a whole tick at a time.
					if (!vt_owns || isNetworkGame)
						update_ship_override(alpha);

					if (anySmoothies)
					{
						float plasma_alpha = alpha;
						if (tick_plasma_ready)
						{
							// The cached plasma is the current tick endpoint. Keep it pristine while
							// foreground commands draw into their separate display buffer.
							memcpy(interp_buf->pixels, plasma_buf->pixels,
							       (size_t)plasma_buf->h * plasma_buf->pitch);
							plasma_alpha = 1.0f;
						}
						else
						{
							// Pass 1: derive this frame's background by filtering a copy of
							// the fixed plasma base with the interpolated backgrounds.
							memcpy(plasma_buf->pixels, bg_feedback->pixels,
							       (size_t)bg_feedback->h * bg_feedback->pitch);
							rl_replay_bg(plasma_buf, alpha, pss, split_bg);
							if (interp_buf != plasma_buf)
							{
								expand_frame_to_hi(plasma_buf, interp_buf, rss / pss);
								rl_replay_bg_tail(interp_buf, alpha, rss);
							}
						}
						// Final stage: entities and overlays at foreground scale. Layer-bound
						// commands share the spatial/temporal phase of the plasma underneath.
						rl_replay_fg(interp_buf, alpha, rss, pss, plasma_alpha, split_bg);
					}
					else
					{
						rl_replay_interp(interp_buf, alpha, false, rss);
					}

					// Zinglon pillar onto the freshly-interpolated frame, centred on the
					// ship's render-rate position so it glides rather than snapping.
					draw_active_zinglon_pillars(interp_buf, rss, true);

					draw_boss_bar_present(interp_buf, rss, alpha);
					hud_special_light_present(interp_buf, rss, alpha);

					if (use_hi)
					{
						// Expand the static HUD, then redraw its three moving gauges at
						// supersampled resolution for sub-pixel motion.
						composite_playfield_hi(interp_buf, vga_hi, rss, true);
						expand_hud_to_hi(VGAScreenSeg, vga_hi, rss);
						if (power_gauge_active)
							draw_power_gauge(vga_hi, rss,
							                 (float)power_render_prev + (power_render_cur - power_render_prev) * alpha,
							                 (salvo_render_prev + (salvo_render_cur - salvo_render_prev) * alpha) / 100.0f);
						gauge_bars_present(vga_hi, rss, alpha);
						JE_drawPerfOverlay(vga_hi, rss);
						present_hi(vga_hi);
					}
					else
					{
						composite_playfield(interp_buf, true);

						// Gauges at the interpolated level: they rise and fall smoothly
						// instead of stepping once per tick.
						if (power_gauge_active)
							draw_power_gauge(VGAScreenSeg, 1,
							                 (float)power_render_prev + (power_render_cur - power_render_prev) * alpha,
							                 (salvo_render_prev + (salvo_render_cur - salvo_render_prev) * alpha) / 100.0f);
						gauge_bars_present(NULL, 1, alpha);

						JE_drawPerfOverlay(VGAScreenSeg, 1);
						JE_showVGA();
					}

					if (!output_vsync)
						limit_render_fps();
					service_SDL_events(false);
				}
				setDelay(frameCountMax);  // keep `target` current for other timing readers
			}
			else
			{
				JE_drawPerfOverlay(VGAScreenSeg, 1);
				JE_showVGA();
				service_wait_delay();
				setDelay(frameCountMax);
			}

			// Advance persistent smoothie feedback once per tick. Store the full background because
			// the next filter reads the previous complete frame.
			if (anySmoothies && bg_feedback != NULL)
			{
				if (tick_plasma_ready)
					memcpy(bg_feedback->pixels, plasma_buf->pixels,
					       (size_t)plasma_buf->h * plasma_buf->pitch);
				else
					rl_replay_bg(bg_feedback, 1.0f, pss, false);
			}
		}
		else
		{
			JE_drawPerfOverlay(VGAScreenSeg, 1);
			JE_showVGA();
		}
	}

	quitRequested = false;
	skipStarShowVGA = false;
}

// Expand the bottom-right playfield edge to the HUD by copying a
// 16-pixel-tall column horizontally for the additional playfield width.
static void extend_playfield_right_column(SDL_Surface* surface)
{
	const int src_x = 262;  // last column of the original playfield
	const int src_y = 184;
	const int height = 16;
	const int copy_width = surface->w - HUD_WIDTH - 263;  // pixels to extend

	Uint8* row = (Uint8*)surface->pixels + src_y * surface->pitch + src_x;
	for (int y = 0; y < height; ++y)
	{
		const Uint8 pixel = row[0];
		memset(row + 1, pixel, copy_width);
		row += surface->pitch;
	}
}

static void copy_screen_to_buffer(Uint8* buffer)
{
	Uint8* src = VGAScreen->pixels;
	for (int y = 0; y < vga_height; ++y)
	{
		memcpy(buffer, src, VGAScreen->pitch);
		buffer += VGAScreen->pitch;
		src += VGAScreen->pitch;
	}
}

static void copy_buffer_to_screen(const Uint8* buffer)
{
	Uint8* dst = VGAScreen->pixels;
	for (int y = 0; y < vga_height; ++y)
	{
		memcpy(dst, buffer, VGAScreen->pitch);
		buffer += VGAScreen->pitch;
		dst += VGAScreen->pitch;
	}
}

// Sub-pixel fraction of tempMapXOfs, set beside every tempMapXOfs assignment so enemies float
// their parallax onto the background layer's sub-pixel offset.
static float tempMapXOfs_frac = 0.0f;
// Background layer whose horizontal anchor tempMapXOfs represents (1..3). The render list uses
// this to resolve legacy draw-order cases where the entity and layer straddle the parallax update.
static int tempMapXOfs_layer = 0;

// Vertical layer phase for enemy sprites and bars, which can straddle the integer ey advance.
static int   tempScrollYBase = 0, tempScrollYBaseBar = 0;
static float tempScrollYfrac = 0.0f, tempScrollYfracBar = 0.0f;
static int   tempScrollYLayer = 0;
// Normal layer step/delay behind the current batch. Full-speed fixedmovey scripts can be
// scaled from the layer's exact integer delta; delay-gated scripts use a percentage carry.
static int   tempScrollBaseStep = 0, tempScrollDelayMax = 1;

// Endless extra scroll for this enemy batch. Track the layer explicitly because layers can share
// the same temporary movement value.
static int tempScrollExtraPx = 0;

// Sky-bank enemies moving at the layer-2 step are attached scenery and inherit that layer's
// Endless and sub-pixel scroll.
static bool skyGlueThisEnemy = false;

// Preserve the previous curLoc interval so event-spawned enemies can catch up when boosted scroll
// crosses multiple event coordinates in one tick.
static bool eventScrollCatchupValid = false;
static int eventScrollFrom = 0, eventScrollTo = 0;
static int eventScrollLayerDelta[4] = { 0, 0, 0, 0 };
static int eventScrollBaseStep[4] = { 0, 0, 0, 0 };
static int eventScrollDelayMax[4] = { 1, 1, 1, 1 };
static int eventScrollBoost = 0;
// Sky-glue spawns use a separate layer clock; capture its carry phase for cross-layer anchoring.
static bool eventScrollSkyValid = false;
static int eventScrollSkyRatio100 = 0;
static int eventScrollSkyPhase100 = 0;

// One-tick-lagged smooth-scroll publication buffers (see the scroll block in the
// level loop). File scope lets the rollback registry snapshot them;
// bgSmoothFracPend feeds eventScrollSkyPhase100, which anchors sky-glue spawns.
static float bgSmoothRatePend[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static float bgSmoothFracPend[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static bool  bgSmoothActivePend  = false;

// Only the part of fixedmovey left after it cancels an opposing eyc is scroll-relative.
static int enemy_scalable_fixed_y(int fixed_move, int own_move)
{
	if (fixed_move < 0 && own_move > 0)
		return fixed_move + own_move < 0 ? fixed_move + own_move : 0;
	if (fixed_move > 0 && own_move < 0)
		return fixed_move + own_move > 0 ? fixed_move + own_move : 0;
	return fixed_move;
}

// Scale a layer-bound fixed-motion residual in the layer's integer phase so exact cancellation
// survives boosted scrolling. Delay-gated motion uses signed percentage carry instead.
static int enemy_fixed_move_y(unsigned int i)
{
	struct JE_SingleEnemyType *const e = &enemy[i];
	const int move = e->fixedmovey;
	const int scalable = enemy_scalable_fixed_y(move, e->eyc);
	const int boost = endlessScrollBoostPercent();

	if (scalable == 0 || tempScrollYLayer == 0 || boost == 0 || tempScrollBaseStep <= 0)
	{
		e->fixedmovey_carry = 0;
		e->fixedmovey_carry_base = 0;
		e->fixedmovey_carry_move = 0;
		return move;
	}

	int divisor, carry_base, numerator;
	if (tempScrollDelayMax == 1)
	{
		// Scale by the layer's ACTUAL base+extra delta this tick. For move == -baseStep,
		// integer division is exact and the sprite remains pixel-locked to the terrain.
		divisor = tempScrollBaseStep;
		carry_base = divisor;
		numerator = scalable * (tempBackMove + tempScrollExtraPx);
	}
	else
	{
		// Stock fixedmovey runs every tick even when the layer's delay gate is closed.
		// Preserve that behavior while scaling its average by the modifier exactly.
		divisor = 100;
		carry_base = -100;  // distinct from a full-speed layer whose step happens to be 100
		numerator = scalable * (100 + boost);
	}

	if (e->fixedmovey_carry_base != carry_base || e->fixedmovey_carry_move != scalable)
	{
		e->fixedmovey_carry = 0;
		e->fixedmovey_carry_base = carry_base;
		e->fixedmovey_carry_move = scalable;
	}

	numerator += e->fixedmovey_carry;
	const int scaled = numerator / divisor;  // signed truncation keeps carry bounded around zero
	e->fixedmovey_carry = numerator - scaled * divisor;
	return (move - scalable) + scaled;
}

// Presentation-only enemy velocity fallback for blinking sprites. Recycled slots discard it.
static int    rl_enemy_hint_px[100], rl_enemy_hint_py[100];
static int    rl_enemy_hint_vx[100], rl_enemy_hint_vy[100];
static Uint32 rl_enemy_hint_gen[100];
static JE_word rl_enemy_hint_type[100];
static Uint32 rl_enemy_gen;   // bumped once per sim pass at the loop top
// Presented frames, for cosmetics that need a steady clock. Rollback re-simulation runs several sim
// passes against one presented frame, so a cadence keyed to rl_enemy_gen stutters.
static Uint32 rl_present_gen;

// Contract in tyrian2.h.
Uint32 rl_presented_frames(void) { return rl_present_gen; }

/* Boss vulnerability cue. See doc/notes.md#health-and-tiers. */

// Presented-frame stamp plus one; 0 means idle.
static Uint32 enemyArmedFlashAt[100];

// In-game palette's greyscale ramp.
#define ENEMY_FLASH_BANK 0x00

// Per-body shade lift passed to blit_enemy.
static Uint8 enemyFlashLift;

bool enemy_armed_flash_arms(JE_byte wasArmor, JE_byte nowArmor, JE_byte avail)
{
	return wasArmor >= 255 && nowArmor > 0 && nowArmor < 255 && avail == 0;
}

bool enemy_armed_flash_shows(JE_byte linknum)
{
	if (vulnerableCue == VULN_CUE_ALL)
		return true;
	return vulnerableCue == VULN_CUE_BOSSES && enemy_has_boss_bar(linknum);
}

Uint8 enemy_armed_flash_lift(Uint32 left)
{
	if (left == 0)
		return 0;
	if (left >= ENEMY_ARMED_FLASH_WHITE)
		return 0x0f;
	return (Uint8)(left * 3);
}

static Uint32 enemy_armed_flash_left(unsigned int slot)
{
	if (enemyArmedFlashAt[slot] == 0)
		return 0;
	const Uint32 elapsed = rl_present_gen + 1 - enemyArmedFlashAt[slot];
	return (elapsed < ENEMY_ARMED_FLASH_FRAMES) ? ENEMY_ARMED_FLASH_FRAMES - elapsed : 0;
}

// Arm after direct armor writes on live passes; damage never calls this path.
static void enemy_note_armed(unsigned int slot, JE_byte wasArmor)
{
	if (vulnerableCue == VULN_CUE_OFF || rollback_resim)
		return;
	if (!enemy_armed_flash_arms(wasArmor, enemy[slot].armorleft, enemyAvail[slot]))
		return;

	enemyArmedFlashAt[slot] = rl_present_gen + 1;
}

// A recycled slot must not inherit the last occupant's cue.
static void enemy_armed_flash_clear(unsigned int slot)
{
	enemyArmedFlashAt[slot] = 0;
}

// Banks the Endless "?" pickup cycles through. Level palettes vary, so the list keeps to hues that
// hold a readable ramp in all of them; bank 0 and the near-black banks are left out.
static const Uint8 endlessSpecialIconBanks[] = { 0x10, 0x20, 0x30, 0x50, 0x70, 0x90, 0xC0, 0xD0 };
#define ENDLESS_SPECIAL_ICON_TICKS 7

// Bank for this frame. Presentation only: rl_present_gen is outside the rollback registry.
static Uint8 endlessSpecialIconFilter(void)
{
	const Uint32 step = (rl_present_gen / ENDLESS_SPECIAL_ICON_TICKS) % COUNTOF(endlessSpecialIconBanks);
	return endlessSpecialIconBanks[step];
}

#define ENDLESS_SPECIAL_SPARK_TICKS 5  // one shower per pickup this often, staggered by enemy slot
#define ENDLESS_SPECIAL_SPARK_COUNT 3
// Reach is also the per-tick velocity, and a spark lives 15 ticks, so this bounds how far the
// shower carries. Matches the density of the superspark weapon trails.
#define ENDLESS_SPECIAL_SPARK_REACH 3

// Shower in the glyph's current colour, thrown from the glyph's centre and hidden underneath it.
// Presentation only: superpixels are outside the rollback registry and JE_doSPSeeded runs its own
// sequence. Silent resim passes must not spawn.
static void endlessSpecialIconSparks(unsigned int i)
{
	if (rollback_resim_silent)
		return;

	// The glyph is drawn well before JE_drawSP, so it hides its own shower through the occluder
	// list instead of through draw order. Published every tick: the sparks in flight were thrown
	// from earlier positions and follow the glyph the pickup has now.
	JE_addSPOccluder(enemy[i].ex + tempMapXOfs + ENDLESS_SPECIAL_GLYPH_INK_X0 - 1,
	                 enemy[i].ey + ENDLESS_SPECIAL_GLYPH_INK_Y0 - 1,
	                 enemy[i].ex + tempMapXOfs + ENDLESS_SPECIAL_GLYPH_INK_X1 + 1,
	                 enemy[i].ey + ENDLESS_SPECIAL_GLYPH_INK_Y1 + 1);

	if ((rl_present_gen % ENDLESS_SPECIAL_SPARK_TICKS) != (i % ENDLESS_SPECIAL_SPARK_TICKS))
		return;

	// Glyph centre, matching the cells the draw places at x_offset -6/+6 and y_offset 0.
	const int cx = enemy[i].ex + tempMapXOfs + 6;
	const int cy = enemy[i].ey + 7;
	if (cx < 0 || cy < 0)
		return;

	// JE_drawSP adds `color` to the plotted shade, so it carries the bank alone. The classic cap
	// stays off: the weapon trails recycle that window several times a second, which would cut a
	// shower this small short of its 15 ticks.
	JE_doSPSeeded((JE_word)cx, (JE_word)cy, ENDLESS_SPECIAL_SPARK_COUNT, ENDLESS_SPECIAL_SPARK_REACH,
	              endlessSpecialIconFilter(), false, ENDLESS_SPARK_BRIGHT, true,
	              rl_present_gen * 100u + i);
}

#define ENDLESS_ELITE_SPARK_TICKS 5  // one shower per elite this often, staggered by enemy slot
#define ENDLESS_ELITE_SPARK_COUNT 1  // champions shed twice this
#define ENDLESS_ELITE_SPARK_REACH 2  // per-tick velocity too, plus one for a 2x2 body

// Bright aura in the tier's own tint (ENDLESS_ELITE_FILTER / ENDLESS_CHAMPION_FILTER), so an elite
// reads as one at a glance. Presentation only: superpixels are outside the rollback registry and
// JE_doSPSeeded runs its own sequence. Silent resim passes must not spawn.
static void endlessEliteAuraSparks(unsigned int i)
{
	if (rollback_resim_silent || enemy[i].eliteState < 2 || enemy[i].iced || enemy[i].edamaged)
		return;
	if ((rl_present_gen % ENDLESS_ELITE_SPARK_TICKS) != (i % ENDLESS_ELITE_SPARK_TICKS))
		return;

	// Sprite centre, the same point for a 1x1 body and a 2x2 one. JE_drawSP clips the rest of the
	// way, so a body hanging off the bottom needs no bound of its own.
	const int cx = enemy[i].ex + tempMapXOfs + 6;
	const int cy = enemy[i].ey + 7;
	if (cx < 0 || cy < 0)
		return;

	const bool champion = (enemy[i].eliteState == 3);
	const int sparks = ENDLESS_ELITE_SPARK_COUNT * (champion ? 2 : 1);

	// Elite auras use a different seed stride from pickup sparks because one orb may emit both.
	// Auras on the special glyph share its occluder.
	JE_doSPSeeded((JE_word)cx, (JE_word)cy, sparks,
	              ENDLESS_ELITE_SPARK_REACH + (enemy[i].size == 1 ? 1 : 0),
	              champion ? ENDLESS_CHAMPION_FILTER : ENDLESS_ELITE_FILTER, false,
	              ENDLESS_SPARK_BRIGHT, endlessSpecialPickup((int)i), rl_present_gen * 137u + i);
}

inline static void blit_enemy(SDL_Surface *surface, unsigned int i, signed int x_offset, signed int y_offset, signed int sprite_offset, bool outline)
{
	if (enemy[i].sprite2s == NULL)
	{
		fprintf(stderr, "warning: enemy %d sprite missing\n", i);
		return;
	}

	const int x = enemy[i].ex + x_offset + tempMapXOfs,
	          y = enemy[i].ey + y_offset;

	// First draw of this slot this tick: derive the per-tick velocity from the
	// last drawn anchor (averaged across a short hidden gap), then re-anchor.
	// Later parts of a multi-cell enemy reuse the cached hint.
	if (rl_enemy_hint_gen[i] != rl_enemy_gen)
	{
		const Uint32 gap = rl_enemy_gen - rl_enemy_hint_gen[i];
		int hvx = 0, hvy = 0;
		if (gap >= 1 && gap <= 3 && rl_enemy_hint_type[i] == enemy[i].enemytype)
		{
			hvx = (x - rl_enemy_hint_px[i]) / (int)gap;
			hvy = (y - rl_enemy_hint_py[i]) / (int)gap;
			if (hvx > 40 || hvx < -40 || hvy > 40 || hvy < -40)
			{
				hvx = 0;
				hvy = 0;
			}
		}
		rl_enemy_hint_vx[i] = hvx;
		rl_enemy_hint_vy[i] = hvy;
		rl_enemy_hint_px[i] = x;
		rl_enemy_hint_py[i] = y;
		rl_enemy_hint_gen[i] = rl_enemy_gen;
		rl_enemy_hint_type[i] = enemy[i].enemytype;
	}

	// Endless special pickups wear the cycling "?" from spriteSheet10 rather than their own art.
	const bool specialPickup = endlessSpecialPickup((int)i);
	Sprite2_array *const sheet = specialPickup ? &spriteSheet10 : enemy[i].sprite2s;

	// enemycycle indexes egr[] 1-based; skip anything that doesn't name a real in-sheet sprite
	// instead of underflowing into a wild read in blit_sprite2.
	const unsigned int cycle = enemy[i].enemycycle;
	if (cycle < 1 || cycle > 20)
		return;
	const unsigned int index = specialPickup
	                         ? (unsigned int)ENDLESS_SPECIAL_PICKUP_ICON + sprite_offset
	                         : enemy[i].egr[cycle - 1] + sprite_offset;
	if (index == 0 || (size_t)index * sizeof(Uint16) > sheet->size)
		return;

	rl_current_id = RL_ID_ENEMY_BASE + (int)i;  // tag for cross-frame interpolation
	rl_current_vel_x = rl_enemy_hint_vx[i];     // fallback velocity when pairing fails (blinkers)
	rl_current_vel_y = rl_enemy_hint_vy[i];
	rl_current_par_frac = tempMapXOfs_frac;     // float the parallax to match the background
	rl_current_par_layer = tempMapXOfs_layer;
	rl_current_par_anchor = (float)(tempMapXOfs - PLAYFIELD_X_SHIFT) + tempMapXOfs_frac;
	rl_current_par_ybase = tempScrollYBase;
	if (skyGlueThisEnemy)
	{
		// Attached sky scenery: bind to layer 2's lagged clock so replay places it with the
		// exact transform the glass rows use (pre-advance phase, batch base is already 0).
		rl_current_par_yfrac = bg_layer_yfrac[2];
		rl_current_par_ylayer = 2;
	}
	else
	{
		rl_current_par_yfrac = tempScrollYfrac;
		rl_current_par_ylayer = tempScrollYLayer;
	}
	// The special-pickup color cycle takes precedence over a hit flash.
	const Uint8 filter = specialPickup ? endlessSpecialIconFilter() : enemy[i].filter;
	if (outline)
		blit_sprite2_solid(surface, x, y, *sheet, index, filter + ENDLESS_SPECIAL_OUTLINE_SHADE);
	else if (enemyFlashLift != 0)
		blit_sprite2_filter_bright(surface, x, y, *sheet, index, ENEMY_FLASH_BANK | enemyFlashLift);
	else if (filter != 0)
		blit_sprite2_filter(surface, x, y, *sheet, index, filter);
	else
		blit_sprite2(surface, x, y, *sheet, index);
	rl_current_id = 0;
	rl_current_vel_x = 0;
	rl_current_vel_y = 0;
	rl_current_par_frac = 0.0f;
	rl_current_par_layer = 0;
	rl_current_par_anchor = 0.0f;
	rl_current_par_ybase = 0;
	rl_current_par_yfrac = 0.0f;
	rl_current_par_ylayer = 0;
}

// Extra off-screen margin for the enemy-draw visibility tests: the interpolated position lags
// the tick position by up to one tick of motion. Equals the interpolation snap threshold.
enum { ENEMY_DRAW_MARGIN = 40 };

// Does a fully opaque 12x14 sprite cell drawn at (x, y) reach into the window? Same inclusive-wx1 /
// exclusive-wy1 convention as sprite2_has_pixel_in_window, so the blank-frame fallback below lands
// on exactly the cells that function would have tested.
static bool sprite_cell_in_window(int x, int y, int wx0, int wx1, int wy0, int wy1)
{
	return x + 11 >= wx0 && x <= wx1 && y + 13 >= wy0 && y < wy1;
}

// Test the current frame's opaque pixels using collision-state coordinates and blit_enemy's frame
// layout. If every gated cell is blank, use the nominal footprint so map-drawn structures such as
// BRAINIAC's walls remain hittable.
static bool enemy_has_visible_pixel(unsigned int i)
{
	if (enemy[i].sprite2s == NULL)
		return false;

	const unsigned int cycle = enemy[i].enemycycle;
	if (cycle < 1 || cycle > 20)  // no real frame selected (matches blit_enemy's guard)
		return false;
	const JE_word gr = enemy[i].egr[cycle - 1];
	if (gr == 999)  // blank frame: JE_drawEnemy treats it as gone, nothing is drawn
		return false;

	const int baseX = enemy[i].ex + enemy[i].mapoffset;  // identical to the collision's X term
	const int baseY = enemy[i].ey;
	const int wx0 = PLAYFIELD_LEFT, wx1 = PLAYFIELD_RIGHT, wy0 = 0, wy1 = vga_height;

	if (enemy[i].size == 1)  // 2x2 enemy: four cells, top/bottom rows gated on ey as in the draw
	{
		const bool topRow = enemy[i].ey > -13 - ENEMY_DRAW_MARGIN;
		const bool botRow = enemy[i].ey > -26 - ENEMY_DRAW_MARGIN && enemy[i].ey < 182 + ENEMY_DRAW_MARGIN;
		const struct { int dx, dy, off; bool on; } cell[4] = {
			{ -6, -7,  0, topRow }, {  6, -7,  1, topRow },
			{ -6,  7, 19, botRow }, {  6,  7, 20, botRow },
		};
		bool anyArt = false, blankCellOnScreen = false;
		for (int c = 0; c < 4; c++)
		{
			if (!cell[c].on)
				continue;
			const int cx = baseX + cell[c].dx, cy = baseY + cell[c].dy;
			const unsigned int index = (unsigned int)gr + cell[c].off;
			if (index == 0 || (size_t)index * sizeof(Uint16) > enemy[i].sprite2s->size)
				continue;
			if (sprite2_has_pixel_in_window(cx, cy, *enemy[i].sprite2s, index, wx0, wx1, wy0, wy1))
				return true;
			if (sprite2_is_blank(*enemy[i].sprite2s, index))
				blankCellOnScreen |= sprite_cell_in_window(cx, cy, wx0, wx1, wy0, wy1);
			else
				anyArt = true;  // a drawn cell exists: its pixels alone decide
		}
		return !anyArt && blankCellOnScreen;
	}
	else  // normal enemy: a single cell, drawn only above the same lower ey bound
	{
		if (!(enemy[i].ey > -13 - ENEMY_DRAW_MARGIN))
			return false;
		const unsigned int index = gr;  // sprite_offset 0
		if (index == 0 || (size_t)index * sizeof(Uint16) > enemy[i].sprite2s->size)
			return false;
		if (sprite2_has_pixel_in_window(baseX, baseY, *enemy[i].sprite2s, index, wx0, wx1, wy0, wy1))
			return true;
		return sprite2_is_blank(*enemy[i].sprite2s, index) &&
		       sprite_cell_in_window(baseX, baseY, wx0, wx1, wy0, wy1);
	}
}

// True when a live enemy is frozen above shot reach. Horizontal movement does
// not count because HARVEST's anchor sways sideways.
static bool enemy_stuck_above_screen(unsigned int i)
{
	return enemy[i].ey   <= -58 &&
	       enemy[i].eyc  <= 0 &&
	       enemy[i].eycc <= 0 &&
	       enemy[i].fixedmovey <= 0;
}

// True when a linked enemy has a reachable partner. Link 0 means unlinked;
// each such enemy stands alone. HARVEST relies on its reachable linked pieces.
static bool enemy_link_group_reachable(unsigned int i)
{
	const JE_byte link = enemy[i].linknum;
	if (link == 0)
		return false;

	for (int e = 0; e < 100; e++)
		if (enemyAvail[e] != 1 && enemy[e].linknum == link && !enemy_stuck_above_screen(e))
			return true;

	return false;
}

// The watchdog targets enemies above shot reach with no reachable link partner.
static bool enemy_stuck_orphaned(unsigned int i)
{
	return enemy_stuck_above_screen(i) && !enemy_link_group_reachable(i);
}

// Count orphaned enemies with a full-pool scan, independent of drawing.
static unsigned int count_stuck_above_screen(void)
{
	unsigned int n = 0;
	for (int i = 0; i < 100; i++)
		if (enemyAvail[i] != 1 && enemy_stuck_orphaned(i))
			++n;
	return n;
}

// Wait about six seconds before culling a stuck-above enemy.
enum { MAP_STOP_STALL_LIMIT = 210 };

// Per-link scenery flags, rebuilt before each homing pass.
static bool endlessLinkHoldsScenery[256];

void endlessScanSceneryLinks(void)
{
	memset(endlessLinkHoldsScenery, 0, sizeof(endlessLinkHoldsScenery));

	for (int i = 0; i < 100; i++)
	{
		if (enemyAvail[i] != 1 && enemy[i].linknum != 0 && !enemy[i].scoreitem &&
		    !endlessEnemyDestructible(enemyAvail[i], enemy[i].linknum, enemy[i].armorleft))
			endlessLinkHoldsScenery[enemy[i].linknum] = true;
	}
}

// Loot keeps its scripted drift, so a homing sector never sends pickups after the ship.
// Linked bodies chase only if every member can eventually be shot.
bool endlessHomingChaser(unsigned int i)
{
	if (enemy[i].scoreitem)
		return false;
	if (!endlessEnemyDestructible(enemyAvail[i], enemy[i].linknum, enemy[i].armorleft))
		return false;
	return enemy[i].linknum == 0 || !endlessLinkHoldsScenery[enemy[i].linknum];
}

static void endlessGroupHoming(void)
{
	if (!endlessFxActive())
		return;

	endlessScanSceneryLinks();

	for (int i = 0; i < 100; i++)
		enemy[i].groupHomed = false;

	for (int i = 0; i < 100; i++)
	{
		if (enemyAvail[i] == 1 || enemy[i].groupHomed || enemy[i].linknum == 0 ||
		    enemy[i].scoreitem || (enemy[i].xaccel == 0 && enemy[i].yaccel == 0))
			continue;

		int members[100];
		int count = 0;
		int sumX = 0;
		int sumY = 0;

		for (int j = i; j < 100; j++)
		{
			if (enemyAvail[j] == 1 || enemy[j].scoreitem ||
			    enemy[j].linknum != enemy[i].linknum ||
			    enemy[j].exc    != enemy[i].exc    || enemy[j].eyc    != enemy[i].eyc ||
			    enemy[j].excc   != enemy[i].excc   || enemy[j].eycc   != enemy[i].eycc ||
			    enemy[j].exccw  != enemy[i].exccw  || enemy[j].eyccw  != enemy[i].eyccw ||
			    enemy[j].xaccel != enemy[i].xaccel || enemy[j].yaccel != enemy[i].yaccel)
				continue;
			members[count++] = j;
			sumX += enemy[j].ex;
			sumY += enemy[j].ey;
		}

		if (count < 2)
			continue;

		const int cx = sumX / count;
		const int cy = sumY / count;

		// The whole group chases the leader's ship, so a formation stays a formation.
		const Player *const prey = &player[endlessHomingTargetPlayer(enemy[i].homeTarget)];

		if (enemy[i].xaccel && enemy[i].xaccel - 89u > mt_rand() % 11)
		{
			if (prey->x - 25 > cx)
			{
				if (enemy[i].exc < enemy[i].xaccel - 89)
					for (int m = 0; m < count; m++)
						enemy[members[m]].exc++;
			}
			else
			{
				if (enemy[i].exc >= 0 || -enemy[i].exc < enemy[i].xaccel - 89)
					for (int m = 0; m < count; m++)
						enemy[members[m]].exc--;
			}
		}

		if (enemy[i].yaccel && enemy[i].yaccel - 89u > mt_rand() % 11)
		{
			if (prey->y > cy)
			{
				if (enemy[i].eyc < enemy[i].yaccel - 89)
					for (int m = 0; m < count; m++)
						enemy[members[m]].eyc++;
			}
			else
			{
				if (enemy[i].eyc >= 0 || -enemy[i].eyc < enemy[i].yaccel - 89)
					for (int m = 0; m < count; m++)
						enemy[members[m]].eyc--;
			}
		}

		for (int m = 0; m < count; m++)
			enemy[members[m]].groupHomed = true;
	}
}

// Base Dispenser volley: one enemy-84 aimed shot plus a four-segment lightning column.
static void dispenser_fire(unsigned int i, JE_integer baseX, JE_integer baseY)
{
	if (cheatNoEnemyFire || endlessReviveGraceActive())
		return;

	const int shotCap = endlessFxActive() ? ENEMY_SHOT_MAX : ENEMY_SHOT_NORMAL;
	const Uint16 w = 59;  // enemy 84's aimed turret weapon

	int spct = 100, dpct = 100;
	if (endlessFxActive())
	{
		spct = endlessShotSpeedPercent();
		dpct = endlessShotDamagePercent();
		if (enemy[i].eliteState == 3)
			dpct = dpct * endlessChampionShotDamagePercent() / 100;
	}
	const Uint8 tint = endlessEliteTint(enemy[i].eliteState);

	// Align the 12x14 aimed shot with the eye and the lightning column with the lower orb.
	const JE_integer eyeX = baseX + 10, eyeY = baseY - 34;
	const JE_integer orbX = baseX + 10, orbY = baseY + 4;

	int b = -1;
	for (int slot = 0; slot < shotCap; ++slot)
		if (enemyShotAvail[slot] == 1)
		{
			b = slot;
			break;
		}
	if (b < 0)
		return;
	enemyShotAvail[b] = 0;

	{
		int sq;
		do
			sq = mt_rand() % 8;
		while (sq == 3);
		soundQueue[sq] = weapons[w].sound;
	}

	enemyShot[b].sx = eyeX + tempMapXOfs;
	enemyShot[b].sy = eyeY;
	enemyShot[b].sdmg = weapons[w].attack[0];
	enemyShot[b].tx = weapons[w].tx;
	enemyShot[b].ty = weapons[w].ty;
	enemyShot[b].duration = weapons[w].del[0];
	enemyShot[b].animate = 0;
	enemyShot[b].animax = weapons[w].weapani;
	enemyShot[b].sgr = weapons[w].sg[0];
	enemyShot[b].seekerArm = 0;
	enemyShot[b].seekerLeft = 0;
	enemyShot[b].filter = tint;
	enemyShot[b].syc = weapons[w].acceleration;
	enemyShot[b].sxc = weapons[w].accelerationx;
	enemyShot[b].sxm = weapons[w].sx[0];
	enemyShot[b].sym = weapons[w].sy[0];

	{
		JE_byte aim = weapons[w].aim;
		if (difficultyLevel > DIFFICULTY_NORMAL)
			aim += difficultyLevel - 2;

		JE_word targetX = player[0].x;
		JE_word targetY = player[0].y;
		if (twoPlayerMode)
		{
			int t;
			if (player[0].is_alive && !player[1].is_alive)
				t = 0;
			else if (player[1].is_alive && !player[0].is_alive)
				t = 1;
			else
				t = mt_rand() % 2;
			if (t == 1)
			{
				targetX = player[1].x - 25;
				targetY = player[1].y;
			}
		}

		JE_integer aimX = (targetX + 25) - eyeX - tempMapXOfs - 4;
		if (aimX == 0)
			aimX = 1;
		JE_integer aimY = targetY - eyeY;
		if (aimY == 0)
			aimY = 1;
		const JE_integer maxMagAim = MAX(abs(aimX), abs(aimY));
		enemyShot[b].sxm = roundf((float)aimX / maxMagAim * aim);
		enemyShot[b].sym = roundf((float)aimY / maxMagAim * aim);
	}

	if (endlessFxActive())
	{
		enemyShot[b].seekerLeft = endlessSeekerPasses();
		if (enemyShot[b].seekerLeft > 0)
			enemyShot[b].seekerArm = 1;
		enemyShot[b].sxm = (enemyShot[b].sxm * spct + (enemyShot[b].sxm >= 0 ? 50 : -50)) / 100;
		enemyShot[b].sym = (enemyShot[b].sym * spct + (enemyShot[b].sym >= 0 ? 50 : -50)) / 100;
		int dmg = (enemyShot[b].sdmg * dpct + 50) / 100;
		enemyShot[b].sdmg = (JE_byte)(dmg > 255 ? 255 : dmg);

		// Tide extra shots fan out from the aimed shot like any other turret volley.
		const int extra = endlessExtraEnemyShots();
		const int fanPhase = endlessFanPhaseNow();
		for (int k = 0; k < extra; ++k)
		{
			int c = -1;
			for (int slot = 0; slot < shotCap; ++slot)
				if (enemyShotAvail[slot] == 1)
				{
					c = slot;
					break;
				}
			if (c < 0)
				break;
			enemyShotAvail[c] = 0;
			enemyShot[c] = enemyShot[b];
			const int fanOrder = fanPhase + k;
			const float fanAng = ((fanOrder & 1) ? -1.0f : 1.0f) * (k / 2 + 1) * 0.20f;
			const float fc = sim_cosf(fanAng), fs = sim_sinf(fanAng);
			const int ox = enemyShot[c].sxm, oy = enemyShot[c].sym;
			enemyShot[c].sxm = roundf(ox * fc - oy * fs);
			enemyShot[c].sym = roundf(ox * fs + oy * fc);
			if (enemyShot[c].sxm == 0 && enemyShot[c].sym == 0)
			{
				enemyShot[c].sxm = ox;
				enemyShot[c].sym = oy;
			}
		}
	}

	// Spawn all four lightning segments together so their animation frames stay aligned.
	static const JE_word boltSegment[4] = { 210, 229, 248, 267 };
	int boltSpeed = 10;

	{
		// The bolt's own voice: weapons 238-242 fire this exact 4-tile sprite set and all
		// of them use S_WEAPON_15.
		int sq;
		do
			sq = mt_rand() % 8;
		while (sq == 3);
		soundQueue[sq] = S_WEAPON_15;
	}

	int boltDmg = weapons[w].attack[0];
	int bolts = 1;
	if (endlessFxActive())
	{
		boltSpeed = (boltSpeed * spct + 50) / 100;
		int dmg = (boltDmg * dpct + 50) / 100;
		boltDmg = dmg > 255 ? 255 : dmg;

		// Rising tide adds complete four-segment bolts, matching the aimed-shot multiplier.
		bolts += endlessExtraEnemyShots();
	}

	// Never emit a partial bolt: cap on the whole bolts the pool can still hold.
	{
		int freeShots = 0;
		for (int slot = 0; slot < shotCap; ++slot)
			if (enemyShotAvail[slot] == 1)
				++freeShots;
		if (bolts > freeShots / 4)
			bolts = freeShots / 4;
	}

	const int boltFanPhase = endlessFanPhaseNow();
	for (int volley = 0; volley < bolts; ++volley)
	{
		// The authored bolt drops straight down; each tide bolt leans off it by the same
		// fan the turret volleys use. Rotating the segment offsets as well as the
		// velocity keeps a leaning bolt a straight line along its own travel.
		float dx = 0.0f, dy = 1.0f;
		if (volley > 0)
		{
			const int fanK = volley - 1;
			const int fanOrder = boltFanPhase + fanK;
			const float fanAng = ((fanOrder & 1) ? -1.0f : 1.0f) * (fanK / 2 + 1) * 0.20f;
			dx = -sim_sinf(fanAng);
			dy = sim_cosf(fanAng);
		}

		for (int s = 0; s < 4; ++s)
		{
			int c = -1;
			for (int slot = 0; slot < shotCap; ++slot)
				if (enemyShotAvail[slot] == 1)
				{
					c = slot;
					break;
				}
			if (c < 0)
				break;
			enemyShotAvail[c] = 0;
			enemyShot[c].sx = orbX + tempMapXOfs + (int)roundf(dx * 14.0f * s);
			enemyShot[c].sy = orbY + (int)roundf(dy * 14.0f * s);
			enemyShot[c].sxm = (int)roundf(dx * boltSpeed);
			enemyShot[c].sym = (int)roundf(dy * boltSpeed);
			enemyShot[c].sxc = 0;
			enemyShot[c].syc = 0;
			enemyShot[c].sdmg = (JE_byte)boltDmg;
			enemyShot[c].tx = 0;
			enemyShot[c].ty = 0;
			enemyShot[c].duration = 255;
			enemyShot[c].animate = 0;
			enemyShot[c].animax = 4;
			enemyShot[c].sgr = boltSegment[s];
			enemyShot[c].seekerArm = 0;
			enemyShot[c].seekerLeft = 0;
			enemyShot[c].filter = tint;
		}
	}
}

// Detect layer-2 sky scenery from its authored eyc/fixedmovey step. Exclude homing enemies and
// falling score items, whose movement can only coincide with the layer rate.
static bool enemy_rides_layer2(const struct JE_SingleEnemyType *e)
{
	return backMove2 > 0 && !e->scoreitem && e->yaccel == 0 &&
	       (int)e->fixedmovey + (e->eycc != 0 ? 0 : (int)e->eyc) == (int)backMove2;
}

void JE_drawEnemy(int enemyOffset) // actually does a whole lot more than just drawing
{
	// JE_drawEnemy(25) is only ever the sky bank (slots 0..24), the one batch whose layer-2
	// ride lives in eyc rather than tempBackMove; the same structural identity the event
	// spawner uses (its case 0 adjusts sky spawns by backMove2).
	const bool skyBank = (enemyOffset == 25);

	player[0].x -= 25;

	for (int i = enemyOffset - 25; i < enemyOffset; i++)
	{
		if (enemyAvail[i] != 1)
		{
			// Settle tier and homing eligibility on the body's first processed frame.
			if (endlessFxActive() && enemy[i].eliteState == 0)
			{
				enemy[i].eliteState = (JE_byte)endlessEliteTierNow(
					enemy[i].linknum, enemy[i].armorleft, enemy[i].scoreitem);

				if (!endlessHomingChaser(i))
				{
					enemy[i].xaccel = enemyDat[enemy[i].enemytype].xaccel;
					enemy[i].yaccel = enemyDat[enemy[i].enemytype].yaccel;
				}
			}

			skyGlueThisEnemy = skyBank && enemy_rides_layer2(&enemy[i]);

			enemy[i].mapoffset = tempMapXOfs;
			enemy[i].mapoffset_frac = tempMapXOfs_frac;  // for the health bar's smooth-H match
			if (skyGlueThisEnemy)
			{
				// The bar records post-advance; pull back the scroll part of this tick's
				// advance (eyc == backMove2 plus the modifier's extra layer-2 pixels).
				enemy[i].scroll_ybase = -((int)backMove2 + endlessScrollExtraPx2);
				enemy[i].scroll_yfrac = bg_layer_yfrac[2];
				enemy[i].scroll_ylayer = 2;
			}
			else
			{
				enemy[i].scroll_ybase = tempScrollYBaseBar;
				enemy[i].scroll_yfrac = tempScrollYfracBar;
				enemy[i].scroll_ylayer = (JE_byte)tempScrollYLayer;
			}

			// The ship this one tracks: its own coin toss in co-op Endless, ship one everywhere else.
			const Player *const prey = &player[endlessHomingTargetPlayer(enemy[i].homeTarget)];

			if (!enemy[i].groupHomed && enemy[i].xaccel && enemy[i].xaccel - 89u > mt_rand() % 11)
			{
				if (prey->x > enemy[i].ex)
				{
					if (enemy[i].exc < enemy[i].xaccel - 89)
						enemy[i].exc++;
				}
				else
				{
					if (enemy[i].exc >= 0 || -enemy[i].exc < enemy[i].xaccel - 89)
						enemy[i].exc--;
				}
			}

			if (!enemy[i].groupHomed && enemy[i].yaccel && enemy[i].yaccel - 89u > mt_rand() % 11)
			{
				if (prey->y > enemy[i].ey)
				{
					if (enemy[i].eyc < enemy[i].yaccel - 89)
						enemy[i].eyc++;
				}
				else
				{
					if (enemy[i].eyc >= 0 || -enemy[i].eyc < enemy[i].yaccel - 89)
						enemy[i].eyc--;
				}
			}

			// Keep 2x2 sprites active until fully outside the visible window. The
			// asymmetric bounds also prevent unclipped left pixels from row-wrapping.
			if (enemy[i].ex + tempMapXOfs > -28 && enemy[i].ex + tempMapXOfs < PLAYFIELD_RIGHT + 38)
			{
				if (enemy[i].aniactive == 1)
				{
					enemy[i].enemycycle++;

					if (enemy[i].enemycycle == enemy[i].animax)
						enemy[i].aniactive = enemy[i].aniwhenfire;
					else if (enemy[i].enemycycle > enemy[i].ani)
						enemy[i].enemycycle = enemy[i].animin;

					// Restored dispenser: the base fires on frame 9, the one frame where the
					// top hatch stands open on the lit eye and the orb below it flares white.
					// Placed inside the advance so it lands exactly once per hatch cycle.
					if (dispenserBasesActive && enemy[i].enemytype == 80 &&
					    enemy[i].enemycycle == 9 && !enemy[i].iced && !enemy[i].edamaged)
						dispenser_fire(i, enemy[i].ex, enemy[i].ey);
				}

				if (enemy[i].enemycycle >= 1 && enemy[i].enemycycle <= 20 &&
				    enemy[i].egr[enemy[i].enemycycle - 1] == 999)
					goto enemy_gone;

				// The flash overrides tint. Choose its scope here because the boss bar may
				// appear after the armor event in the same tick.
				enemyFlashLift = enemy_armed_flash_shows(enemy[i].linknum)
				               ? enemy_armed_flash_lift(enemy_armed_flash_left(i))
				               : 0;
				if (enemyFlashLift == 0 && enemy[i].filter == 0)
					enemy[i].filter = enemy_body_tint(i);

				endlessEliteAuraSparks(i);

				if (endlessSpecialPickup((int)i))
				{
					// Top half of the icon only, at y_offset 0 so the glyph lands on the centre a
					// full 2x2 pickup would have occupied.
					if (enemy[i].ey > -13 - ENEMY_DRAW_MARGIN && enemy[i].ey < 182 + ENEMY_DRAW_MARGIN)
					{
						// The glyph straddles both cells, so every outline pass must land before
						// either glyph cell; per-cell order lets one outline notch the other's art.
						static const struct { int dx, dy; } outline[] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
						for (unsigned int o = 0; o < COUNTOF(outline); ++o)
						{
							blit_enemy(VGAScreen, i, -6 + outline[o].dx, outline[o].dy, 0, true);
							blit_enemy(VGAScreen, i,  6 + outline[o].dx, outline[o].dy, 1, true);
						}

						blit_enemy(VGAScreen, i, -6, 0, 0, false);
						blit_enemy(VGAScreen, i,  6, 0, 1, false);
						endlessSpecialIconSparks(i);
					}
				}
				else if (enemy[i].size == 1) // 2x2 enemy
				{
					if (enemy[i].ey > -13 - ENEMY_DRAW_MARGIN)
					{
						blit_enemy(VGAScreen, i, -6, -7, 0, false);
						blit_enemy(VGAScreen, i,  6, -7, 1, false);
					}
					if (enemy[i].ey > -26 - ENEMY_DRAW_MARGIN && enemy[i].ey < 182 + ENEMY_DRAW_MARGIN)
					{
						blit_enemy(VGAScreen, i, -6,  7, 19, false);
						blit_enemy(VGAScreen, i,  6,  7, 20, false);
					}
				}
				else
				{
					if (enemy[i].ey > -13 - ENEMY_DRAW_MARGIN)
						blit_enemy(VGAScreen, i, 0, 0, 0, false);
				}

				enemy[i].filter = 0;
				enemyFlashLift = 0;
			}

			if (enemy[i].excc)
			{
				if (--enemy[i].exccw <= 0)
				{
					if (enemy[i].exc == enemy[i].exrev)
					{
						enemy[i].excc = -enemy[i].excc;
						enemy[i].exrev = -enemy[i].exrev;
						enemy[i].exccadd = -enemy[i].exccadd;
					}
					else
					{
						enemy[i].exc += enemy[i].exccadd;
						enemy[i].exccw = enemy[i].exccwmax;
						if (enemy[i].exc == enemy[i].exrev)
						{
							enemy[i].excc = -enemy[i].excc;
							enemy[i].exrev = -enemy[i].exrev;
							enemy[i].exccadd = -enemy[i].exccadd;
						}
					}
				}
			}

			if (enemy[i].eycc)
			{
				if (--enemy[i].eyccw <= 0)
				{
					if (enemy[i].eyc == enemy[i].eyrev)
					{
						enemy[i].eycc = -enemy[i].eycc;
						enemy[i].eyrev = -enemy[i].eyrev;
						enemy[i].eyccadd = -enemy[i].eyccadd;
					}
					else
					{
						enemy[i].eyc += enemy[i].eyccadd;
						enemy[i].eyccw = enemy[i].eyccwmax;
						if (enemy[i].eyc == enemy[i].eyrev)
						{
							enemy[i].eycc = -enemy[i].eycc;
							enemy[i].eyrev = -enemy[i].eyrev;
							enemy[i].eyccadd = -enemy[i].eyccadd;
						}
					}
				}
			}

			// Fixed movement has mixed level-script semantics: sky values are local motion, while
			// layer-bound values often cancel/modify the normal layer advance. Scale only the latter,
			// directly from this batch's exact layer delta (never from an independent boost pulse).
			enemy[i].ey += enemy_fixed_move_y(i);

			enemy[i].ex += enemy[i].exc;
			if (enemy[i].ex < -80 || enemy[i].ex > vga_width + 20)
				goto enemy_gone;

			enemy[i].ey += enemy[i].eyc;
			if (enemy[i].ey < -112 || enemy[i].ey > 190)
				goto enemy_gone;

			goto enemy_still_exists;

enemy_gone:
			/* enemy[i].egr[10] &= 0x00ff; <MXD> madness? */
			enemyAvail[i] = 1;
			goto draw_enemy_end;

enemy_still_exists:

			/*X bounce*/
			if (enemy[i].ex <= enemy[i].xminbounce || enemy[i].ex >= enemy[i].xmaxbounce)
				enemy[i].exc = -enemy[i].exc;

			/*Y bounce*/
			if (enemy[i].ey <= enemy[i].yminbounce || enemy[i].ey >= enemy[i].ymaxbounce)
				enemy[i].eyc = -enemy[i].eyc;

			/* Evalue != 0 - score item at boundary */
			// Keep pickups inside the VISIBLE window (vanilla: -5..245 vs window 0..262).
			// The old -5/PLAYFIELD_WIDTH pair let items park fully hidden in the cropped
			// left margin, and stopped them ~24px short of the widened right edge.
			if (enemy[i].scoreitem)
			{
				if (enemy[i].ex < PLAYFIELD_LEFT - 5)
					enemy[i].ex++;
				if (enemy[i].ex > PLAYFIELD_RIGHT - 17)
					enemy[i].ex--;
			}

			// Scroll-track at overclock pace using the SMOOTH per-tick px of whichever layer this enemy
			// rides (tagged per batch beside tempBackMove), so ground enemies glide with the terrain
			// instead of drifting when two layers share a backMove.
			enemy[i].ey += tempBackMove + tempScrollExtraPx +
			               (skyGlueThisEnemy ? endlessScrollExtraPx2 : 0);

			if (enemy[i].ex <= -24 || enemy[i].ex >= vga_width - 24)
				goto draw_enemy_end;

			JE_integer tempX = enemy[i].ex;
			JE_integer tempY = enemy[i].ey;

			temp = enemy[i].enemytype;

			/* Enemy Shots */
			if (enemy[i].edamaged == 1)
				goto draw_enemy_end;

			enemyOnScreen++;

			if (enemy[i].iced)
			{
				enemy[i].iced--;
				if (enemy[i].enemyground != 0)
				{
					enemy[i].filter = 0x09;
				}
				goto draw_enemy_end;
			}

			for (int j = 3; j > 0; j--)
			{
				if (enemy[i].freq[j-1])
				{
					temp3 = enemy[i].tur[j-1];

					if (--enemy[i].eshotwait[j-1] == 0 && temp3)
					{
						enemy[i].eshotwait[j-1] = enemy[i].freq[j-1];
						if (difficultyLevel > DIFFICULTY_NORMAL)
						{
							enemy[i].eshotwait[j-1] = (enemy[i].eshotwait[j-1] / 2) + 1;
							if (difficultyLevel > DIFFICULTY_MANIACAL)
								enemy[i].eshotwait[j-1] = (enemy[i].eshotwait[j-1] / 2) + 1;
						}

						if (endlessFxActive())
						{
							// Endless: enemies fire faster with depth (lower cooldown = faster);
							// champions fire faster still.
							int fd = endlessFireDelayPercent();
							if (enemy[i].eliteState == 3)
								fd = fd * endlessChampionFireDelayPercent() / 100;
							enemy[i].eshotwait[j-1] = enemy[i].eshotwait[j-1] * fd / 100;
							if (enemy[i].eshotwait[j-1] < 1)
								enemy[i].eshotwait[j-1] = 1;
						}

						if (galagaMode && (enemy[i].eyc == 0 || (mt_rand() % 400) >= galagaShotFreq))
							goto draw_enemy_end;

						switch (temp3)
						{
						case 252: /* Savara Boss DualMissile */
							if (enemy[i].ey > 20)
							{
								JE_setupExplosion(tempX - 8 + tempMapXOfs, tempY - 20 - backMove * 8, -2, 6, false, false);
								JE_setupExplosion(tempX + 4 + tempMapXOfs, tempY - 20 - backMove * 8, -2, 6, false, false);
							}
							break;
						case 251:; /* Suck-O-Magnet */
							const JE_integer attraction = 4 - (abs(player[0].x - tempX) + abs(player[0].y - tempY)) / 100;
							if (attraction > 0)
								player[0].x_velocity += (player[0].x > tempX) ? -attraction : attraction;
							break;
						case 253: /* Left ShortRange Magnet */
							if (abs(player[0].x + 25 - 14 - tempX) < 24 && abs(player[0].y - tempY) < 28)
							{
								player[0].x_velocity += 2;
							}
							if (twoPlayerMode &&
							   (abs(player[1].x - 14 - tempX) < 24 && abs(player[1].y - tempY) < 28))
							{
								player[1].x_velocity += 2;
							}
							break;
						case 254: /* Left ShortRange Magnet */
							if (abs(player[0].x + 25 - 14 - tempX) < 24 && abs(player[0].y - tempY) < 28)
							{
								player[0].x_velocity -= 2;
							}
							if (twoPlayerMode &&
							   (abs(player[1].x - 14 - tempX) < 24 && abs(player[1].y - tempY) < 28))
							{
								player[1].x_velocity -= 2;
							}
							break;
						case 255: /* Magneto RePulse!! */
							if (difficultyLevel != DIFFICULTY_EASY) /*DIF*/
							{
								if (j == 3)
								{
									enemy[i].filter = 0x70;
								}
								else
								{
									const JE_integer repulsion = 4 - (abs(player[0].x - tempX) + abs(player[0].y - tempY)) / 20;
									if (repulsion > 0)
										player[0].x_velocity += (player[0].x > tempX) ? repulsion : -repulsion;
								}
							}
							break;
						default:
						/*Rot*/
							if (cheatNoEnemyFire)  // debug: enemies behave but don't shoot
								break;
							// Revive grace suppresses bullets, not turret cooldowns or non-firing
							// behavior such as magnets and launch puffs.
							if (endlessReviveGraceActive())
								break;
							// `multi` may describe one tiled projectile or beam. Carry tide
							// credit between shots and emit only complete authored volleys.
							const int endlessBaseMulti = weapons[temp3].multi;
							int endlessExtraVolleys = 0;
							if (endlessFxActive() && endlessBaseMulti > 0)
							{
								const int credit = enemy[i].eshotextracredit[j-1] + endlessExtraEnemyShots();
								endlessExtraVolleys = credit / endlessBaseMulti;
								enemy[i].eshotextracredit[j-1] = (JE_byte)(credit % endlessBaseMulti);
							}

							// Only endless draws on the enlarged enemy-shot pool; normal levels keep the
							// original 60-slot cap so they play exactly as before.
							const int enemyShotCap = endlessFxActive() ? ENEMY_SHOT_MAX : ENEMY_SHOT_NORMAL;
							if (endlessFxActive() && endlessBaseMulti > 0)
							{
								// Do not truncate a composite volley when the pool is nearly full.
								int freeShots = 0;
								for (int slot = 0; slot < enemyShotCap; ++slot)
									if (enemyShotAvail[slot] == 1)
										++freeShots;

								const int completeVolleys = freeShots / endlessBaseMulti;
								if (completeVolleys == 0)
									goto draw_enemy_end;
								if (endlessExtraVolleys > completeVolleys - 1)
									endlessExtraVolleys = completeVolleys - 1;
							}

							// Tide fan lean: held for a full second of game time instead of
							// alternating per volley, so a burst reads as one sweep.
							const int endlessFanPhase = endlessFanPhaseNow();

							const int endlessVolley = endlessBaseMulti * (1 + endlessExtraVolleys);
							int baseShotSlots[WEAPON_MULTI_MAX];
							for (int shotNum = 0; shotNum < endlessVolley; shotNum++)
							{
								for (b = 0; b < enemyShotCap; b++)
								{
									if (enemyShotAvail[b] == 1)
										break;
								}
								if (b == enemyShotCap)
									goto draw_enemy_end;

								enemyShotAvail[b] = !enemyShotAvail[b];

								if (shotNum >= endlessBaseMulti)
								{
									// Clone the current volley without advancing its weapon pattern.
									// One fan angle keeps all of a composite shot's parts together.
									const int component = shotNum % endlessBaseMulti;
									enemyShot[b] = enemyShot[baseShotSlots[component]];

									const int fanK = shotNum / endlessBaseMulti - 1;
									const int fanOrder = endlessFanPhase + fanK;
									const float fanAng = ((fanOrder & 1) ? -1.0f : 1.0f) * (fanK / 2 + 1) * 0.20f;
									const float fc = sim_cosf(fanAng), fs = sim_sinf(fanAng);
									const int ox = enemyShot[b].sxm, oy = enemyShot[b].sym;
									enemyShot[b].sxm = roundf(ox * fc - oy * fs);
									enemyShot[b].sym = roundf(ox * fs + oy * fc);
									if (enemyShot[b].sxm == 0 && enemyShot[b].sym == 0)
									{
										enemyShot[b].sxm = ox;
										enemyShot[b].sym = oy;
									}
									continue;
								}

								baseShotSlots[shotNum] = b;

								if (weapons[temp3].sound > 0)
								{
									do
									{
										temp = mt_rand() % 8;
									} while (temp == 3);
									soundQueue[temp] = weapons[temp3].sound;
								}

								if (enemy[i].aniactive == 2)
									enemy[i].aniactive = 1;

								if (++enemy[i].eshotmultipos[j-1] > weapons[temp3].max)
									enemy[i].eshotmultipos[j-1] = 1;

								int tempPos = enemy[i].eshotmultipos[j-1] - 1;

								if (j == 1)
									temp2 = 4;

								enemyShot[b].sx = tempX + weapons[temp3].bx[tempPos] + tempMapXOfs;
								enemyShot[b].sy = tempY + weapons[temp3].by[tempPos];
								enemyShot[b].sdmg = weapons[temp3].attack[tempPos];
								enemyShot[b].tx = weapons[temp3].tx;
								enemyShot[b].ty = weapons[temp3].ty;
								enemyShot[b].duration = weapons[temp3].del[tempPos];
								enemyShot[b].animate = 0;
								enemyShot[b].animax = weapons[temp3].weapani;

								enemyShot[b].sgr = weapons[temp3].sg[tempPos];
								enemyShot[b].seekerArm = 0;
								enemyShot[b].seekerLeft = 0;
								// An elite or champion fires in its own bank, so its bullets read as
								// dangerous as the body they came from. 0 outside an endless run.
								enemyShot[b].filter = endlessEliteTint(enemy[i].eliteState);
								switch (j)
								{
								case 1:
									enemyShot[b].syc = weapons[temp3].acceleration;
									enemyShot[b].sxc = weapons[temp3].accelerationx;

									enemyShot[b].sxm = weapons[temp3].sx[tempPos];
									enemyShot[b].sym = weapons[temp3].sy[tempPos];
									break;
								case 3:
									enemyShot[b].sxc = -weapons[temp3].acceleration;
									enemyShot[b].syc = weapons[temp3].accelerationx;

									enemyShot[b].sxm = -weapons[temp3].sy[tempPos];
									enemyShot[b].sym = -weapons[temp3].sx[tempPos];
									break;
								case 2:
									enemyShot[b].sxc = weapons[temp3].acceleration;
									enemyShot[b].syc = -weapons[temp3].acceleration;

									enemyShot[b].sxm = weapons[temp3].sy[tempPos];
									enemyShot[b].sym = -weapons[temp3].sx[tempPos];
									break;
								}

								if (weapons[temp3].aim > 0)
								{
									JE_byte aim = weapons[temp3].aim;

									/*DIF*/
									if (difficultyLevel > DIFFICULTY_NORMAL)
										aim += difficultyLevel - 2;

									JE_word targetX = player[0].x;
									JE_word targetY = player[0].y;

									if (twoPlayerMode)
									{
										// fire at live player(s)
										if (player[0].is_alive && !player[1].is_alive)
											temp = 0;
										else if (player[1].is_alive && !player[0].is_alive)
											temp = 1;
										else
											temp = mt_rand() % 2;

										if (temp == 1)
										{
											targetX = player[1].x - 25;
											targetY = player[1].y;
										}
									}

									JE_integer aimX = (targetX + 25) - tempX - tempMapXOfs - 4;
									if (aimX == 0)
										aimX = 1;
									JE_integer aimY = targetY - tempY;
									if (aimY == 0)
										aimY = 1;
									const JE_integer maxMagAim = MAX(abs(aimX), abs(aimY));
									enemyShot[b].sxm = roundf((float)aimX / maxMagAim * aim);
									enemyShot[b].sym = roundf((float)aimY / maxMagAim * aim);
								}

								if (endlessFxActive())
								{
									enemyShot[b].seekerLeft = endlessSeekerPasses();
									if (enemyShot[b].seekerLeft > 0)
										enemyShot[b].seekerArm = 1;

									// Endless: enemy projectiles get faster with depth. Scale both
									// velocity components (set above for fixed-direction and aimed
									// shots alike); round away from zero so slow shots still speed up.
									int spct = endlessShotSpeedPercent();
									enemyShot[b].sxm = (enemyShot[b].sxm * spct + (enemyShot[b].sxm >= 0 ? 50 : -50)) / 100;
									enemyShot[b].sym = (enemyShot[b].sym * spct + (enemyShot[b].sym >= 0 ? 50 : -50)) / 100;

									// ...and hit harder with depth (DEVASTATING sector adds more; champion
									// shooters add more still). sdmg is a byte fed straight to
									// JE_playerDamage; round to nearest and clamp so it can't wrap past 255.
									int dpct = endlessShotDamagePercent();
									if (enemy[i].eliteState == 3)
										dpct = dpct * endlessChampionShotDamagePercent() / 100;
									int dmg = (enemyShot[b].sdmg * dpct + 50) / 100;
									enemyShot[b].sdmg = (JE_byte)(dmg > 255 ? 255 : dmg);

								}
							}
							break;
						}
					}
				}
			}

			/* Enemy Launch Routine */
			if (enemy[i].launchfreq)
			{
				if (--enemy[i].launchwait == 0)
				{
					enemy[i].launchwait = enemy[i].launchfreq;

					if (enemy[i].launchspecial != 0)
					{
						/*Type  1 : Must be inline with player*/
						if (abs(enemy[i].ey - player[0].y) > 5)
							goto draw_enemy_end;
					}

					if (enemy[i].aniactive == 2)
					{
						enemy[i].aniactive = 1;
					}

					if (enemy[i].launchtype == 0)
						goto draw_enemy_end;

					tempW = enemy[i].launchtype;
					b = JE_newEnemy(enemyOffset == 50 ? 75 : enemyOffset - 25, tempW, 0);

					/*Launch Enemy Placement*/
					if (b > 0)
					{
						struct JE_SingleEnemyType* e = &enemy[b-1];

						e->ex = tempX;
						e->ey = tempY + enemyDat[e->enemytype].startyc;
						if (e->size == 0)
							e->ey -= 7;

						if (e->launchtype > 0 && e->launchfreq == 0)
						{
							if (e->launchtype > 90)
							{
								e->ex += mt_rand() % ((e->launchtype - 90) * 4) - (e->launchtype - 90) * 2;
							}
							else
							{
								JE_integer aimX = (player[0].x + 25) - tempX - tempMapXOfs - 4;
								if (aimX == 0)
									aimX = 1;
								JE_integer aimY = player[0].y - tempY;
								if (aimY == 0)
									aimY = 1;
								const JE_integer maxMagAim = MAX(abs(aimX), abs(aimY));
								e->exc = roundf((float)aimX / maxMagAim * e->launchtype);
								e->eyc = roundf((float)aimY / maxMagAim * e->launchtype);
							}
						}

						do
						{
							temp = mt_rand() % 8;
						} while (temp == 3);
						soundQueue[temp] = randomEnemyLaunchSounds[(mt_rand() % 3)];

						if (enemy[i].launchspecial == 1 &&
						    enemy[i].linknum < 100)
						{
							e->linknum = enemy[i].linknum;
						}
					}
				}
			}
		}
draw_enemy_end:
		;
	}

	player[0].x += 25;
}

/* Which slot the LAST LEVEL backup lives in. Keyed on the session, not on twoPlayerMode: an
 * ENGAGE mini-game (']e') and every galaga level end drop that flag mid-game, and an online pair
 * that fell back to the solo slot would each reload a different local save and desync. */
static JE_byte backup_save_slot(void)
{
	return (twoPlayerMode || isNetworkGame) ? 22 : 11;
}

/* Draw and claim one opaque warning strip for residual replay. The lettering stays unclaimed
 * because its bounding box contains playfield pixels that must keep interpolating. */
static void warning_bar(int x0, int y0, int x1, int y1, Uint8 col)
{
	fill_rectangle_xy(VGAScreen, x0, y0, x1, y1, col);
	rl_mark_overlay_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

void JE_main(void)
{
	char buffer[256];

	int lastEnemyOnScreen;

	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: JE_main entered\n");
		fflush(stderr);
	}

	/* Initial Endless outpost. */
	if (endlessMode)
	{
		// First level of the run: run the starting shop before level 1 (the normal between-
		// level flow only runs after a level clears). The shop's Start Level submenu IS the
		// course choice; it sets mainLevel + the mutators and launches.
		endlessBetweenLevels();

		// Player chose Quit Game in the shop instead of charting a course: end the run and
		// return to the title. On quit the shop leaves mainLevel == 0 (and has already faded out).
		if (mainLevel == 0)
		{
			endlessEndRunToTitle();  // hardcore shows the Run Over summary first
			return;
		}
	}

	goto start_level_first;

	/* Level transition. */

	/* Startlevel is called after a previous level is over.  If the first level
	   is started for a gaming session, startlevelfirst is called instead and
	   this code is skipped.  The code here finishes the level and prepares for
	   the loadmap function. */

start_level:

	// Leaving the level loop: whatever rollback machinery was live is done.
	rollback_level_end();

	// A script's music fade (event 34) drops the MASTER volume and is normally undone by the event
	// 35 that follows it.
	musicFade = false;
	set_volume(tyrMusicVolume, fxVolume);

	mouseSetRelative(false);

	if (galagaMode && !coop_mode_active())
		twoPlayerMode = false;

	JE_clearKeyboard();

	free_sprite2s(&enemySpriteSheets[0]);
	free_sprite2s(&enemySpriteSheets[1]);
	free_sprite2s(&enemySpriteSheets[2]);
	free_sprite2s(&enemySpriteSheets[3]);

	/* Normal speed */
	if (fastPlay != 0)
	{
		smoothScroll = true;
		Uint16 speed = 0x4300;
		setDelaySpeed(speed);
	}

	if (play_demo || record_demo)
	{
		if (demo_file)
		{
			fclose(demo_file);
			demo_file = NULL;
		}

		if (play_demo)
		{
			moveTyrianLogoUp = true;
			stop_song();
			fade_black(10);

			wait_noinput(true, true, true);
			newkey = false;
			newmouse = false;
		}
	}

	if (difficulty_adjust_active())
		difficultyLevel = oldDifficultyLevel;   /*Return difficulty to normal*/

	if (!play_demo)
	{
		// Endless "Quit Level": don't end the run. Revert to the launch-time snapshot and reopen the
		// buy/sell menu LOCKED to those choices; relaunch the same level, or save/load, or quit the
		// run. No depth++ and no level-clear screen: this is a retry, not a completed zone.
		if (endlessMode && endlessQuitToOutpost && endlessSortieValid())
		{
			endlessQuitToOutpost = false;
			fade_song();
			fade_black(10);
			endlessRestoreSortie();   // revert player loadout + endless state; arms the locked outpost
			endlessBetweenLevels();   // reopens the shop (locked); sets mainLevel on relaunch, 0 on quit-run
			if (mainLevel == 0)          // player chose Quit Game in the locked outpost: end the run and
			{                            // return to the title (hardcore shows the Run Over summary first,
				endlessEndRunToTitle();  // since a hardcore quit is as final as a death).
				return;
			}
			goto start_level_first;   // re-run the same level (endlessCaptureSortie re-snapshots + clears the lock)
		}

		// Was the level that just ran picked straight out of the debug level browser? Consume the
		// flag whatever the answer, so it can never carry over to a campaign-reached level.
		const bool fromDebugBrowser = debugLevelJumpTake();

		const bool cleared = (!all_players_dead() || normalBonusLevelCurrent || bonusLevelCurrent) && !playerEndLevel;

		// ENGAGE quits and deaths return to the originating outpost. Cleared TIME WAR
		// continues into SuperTyrian on both peers.
		if (fromDebugBrowser && engageMode && !(cleared && !galagaMode))
		{
			if (cleared)
				JE_endLevelAni();
			fade_song();
			if (!cleared)
				fade_black(10);

			debugLevelJumpReturn();
			// Another level reached without passing through the outpost; same handshake owed.
			network_level_rendezvous();
			goto start_level_first;
		}

		// The galaga ENGAGE rounds (** ALE ** and SQUADRON) are authored endless: dying or
		// quitting is the only way out. If a modified map ever runs one dry, an online pair
		// takes the quit route back to the outpost rather than dragging galaga flags on.
		const bool engageGalagaEnd = engageMode && galagaMode && coop_mode_active();

		if (cleared && !engageGalagaEnd)
		{
			if (qa_net_gameplay_ticks > 0)
			{
				++qa_net_zones_cleared;
				fprintf(stderr, "net gameplay: level cleared (%d of %d)\n",
				        qa_net_zones_cleared, qa_net_zones);
				fflush(stderr);
			}

			if (endlessMode)
			{
				endlessRunDepth++;
				endlessOnSectorCleared();  // bank the boons that pay out at the NEXT outpost (Star Charts / Breakthrough)
			}
			else
			{
				mainLevel = nextLevel;

#ifdef WITH_NETWORK
				/* Wire campaign run: after the second-to-last scheduled clear, drive the episode's own ]Q
				 * transition (episode 1 section 26) instead of flying the rest of the episode; the final
				 * scheduled level is then the next episode's first. */
				if (qa_net_gameplay_ticks > 0 && qa_net_zones > 0 && episodeNum == 1
				    && network_game_type == NETWORK_GAME_CAMPAIGN
				    && qa_net_zones_cleared == qa_net_zones - 1)
				{
					mainLevel = 26;
					fprintf(stderr, "net gameplay: driving the episode transition\n");
					fflush(stderr);
				}
#endif
			}

			JE_endLevelAni();  // level-complete screen first...

			fade_song();

			if (endlessMode)
			{
				endlessBetweenLevels();  // ...then the between-level shop, whose Start Level submenu is the
				                         // course choice: it sets mainLevel + the mutators and launches.

				// Player chose Quit Game instead of a course: end the run, back to the title.
				if (mainLevel == 0)
				{
					endlessEndRunToTitle();  // hardcore shows the Run Over summary first
					return;
				}
			}
		}
		else
		{
			// Endless death in Relaxed: the frozen death frame gets a choice before the run summary.
			EndlessDeathChoice deathPick = ENDLESS_DEATH_END_RUN;

			// The blocking death menu fades and restores its own music. Endless keeps
			// the track for the run summary, which handles its own fade.
			if (endlessDeathMenuDue() && all_players_dead())
			{
				// One run, so one decision: the host chooses for the pair and the joiner adopts it.
				if (endlessCoop() && thisPlayerNum != networkHostPlayerNum)
				{
					// A panel over the frozen death frame, like the one the pause menu puts up for a partner
					// in the options screen: the wait is modal, so it belongs where the prompt the host is
					// reading would be, not in the bottom message bar.
					SDL_Surface *const death_surface = VGAScreen;
					VGAScreen = VGAScreenSeg;   /* side-effect of game_screen; display space */
					JE_drawNetworkNotice("Waiting for the host to choose.");
					JE_showVGA();
					const int adopted = network_endless_death_sync(-1);
					VGAScreen = death_surface;
					deathPick = (adopted >= 0 && adopted <= ENDLESS_DEATH_END_RUN)
					          ? (EndlessDeathChoice)adopted : ENDLESS_DEATH_END_RUN;
				}
				else
				{
					deathPick = JE_endlessDeathMenu();
					if (endlessCoop())
						network_endless_death_sync((int)deathPick);
				}
			}
			else if (!endlessMode)
				fade_song();

			fade_black(10);

			if (endlessMode && deathPick == ENDLESS_DEATH_RESTART)
			{
				endlessRestartSortie();  // revert to the launch snapshot and re-arm the same zone

				// This path reaches a level without going through the outpost, so it owes the
				// level-start handshake the shop would otherwise have done.
				if (endlessCoop())
					network_level_rendezvous();

				// Force a same-song retry to reload after start_level_first fades the current track.
				clear_song_selection();
				goto start_level_first;
			}

			if (endlessMode)
			{
				// Return to Outpost reverts to the same launch-time snapshot, then reopens the shop
				// the way Quit Level does.
				if (deathPick == ENDLESS_DEATH_OUTPOST)
				{
					endlessRestoreSortie();
					endlessBetweenLevels();
					if (mainLevel == 0)  // player chose Quit Game in the reopened outpost
					{
						endlessEndRunToTitle();
						return;
					}
					goto start_level_first;
				}

				endlessOnRunEnd();
				endlessMode = false;
				mainLevel = 0;
				return;
			}

			if (timedBattleMode)
			{
				// Both racers went down before the clock did; the run still had a winner, and
				// this path never reaches the level-complete tally that would have shown it.
				if (isNetworkGame)
					JE_timedBattleResult();
				mainLevel = 0;
				return;
			}

			// A campaign death retries the level. This reload puts both wallets back to their
			// level-start values, so nothing is banked or recorded here.
			JE_loadGame(backup_save_slot());
			if (doNotSaveBackup)
			{
				superTyrian = false;
				onePlayerAction = false;
				player[0].items.super_arcade_mode = SA_NONE;
			}
			if (bonusLevelCurrent && !playerEndLevel)
			{
				mainLevel = nextLevel;
			}
		}
	}
	doNotSaveBackup = false;

	if (play_demo)
		return;

start_level_first:

	set_volume(tyrMusicVolume, fxVolume);

	endLevel = false;
	reallyEndLevel = false;
	playerEndLevel = false;
	extraGame = false;

	doNotSaveBackup = false;
	JE_loadMap();

	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: JE_loadMap returned (mainLevel %d)\n", (int)mainLevel);
		fflush(stderr);
	}

	if (mainLevel == 0)  // if quit itemscreen
		return;          // back to titlescreen

	// An endless run just loaded from within a shop resumes at its OUTPOST, not by dropping
	// straight into the loaded level. (Title-screen loads already ran the outpost at JE_main's
	// entry; this covers the buy/sell-menu load, where JE_loadMap bailed out above.)
	if (endlessMode && endlessResumePending())
	{
		gameLoaded = false;      // consumed; don't let the shop see it and close instantly
		endlessBetweenLevels();  // reopens the SAVED outpost snapshot (no reroll)
		if (mainLevel == 0)      // player quit the outpost
		{
			endlessEndRunToTitle();  // hardcore shows Run Over first (a resumed run is never hardcore, but keep it uniform)
			return;
		}
		goto start_level_first;
	}

	if (endlessMode)
	{
		endlessRegenerateLevel();
		endlessCaptureSortie();  // snapshot the launch-time loadout + committed level for a possible Quit Level retry
		endlessNoteZoneReached(endlessRunDepth + 1);  // launching a zone IS reaching it: advance the all-time record
		if (qa_net_gameplay_ticks > 0)
		{
			fprintf(stderr, "net gameplay: endless level regenerated\n");
			fflush(stderr);
		}
	}
	else
		endlessCampaignLevelStart();  // debug campaign mods: the effect layer's per-level reset (no-op when off)

	crashlog_set_phase("playing level");

	if (!play_demo)
		mouseSetRelative(true);

	fade_song();

	for (uint i = 0; i < COUNTOF(player); ++i)
		player[i].is_alive = true;

	oldDifficultyLevel = difficultyLevel;
	if (difficulty_adjust_active())
	{
		if (episodeNum == EPISODE_AVAILABLE)
			difficultyLevel--;
		if (difficultyLevel < DIFFICULTY_EASY)
			difficultyLevel = DIFFICULTY_EASY;
	}

	player[0].x = 100;
	player[0].y = 180;

	player[1].x = 190;
	player[1].y = 180;

	assert(COUNTOF(player->old_x) == COUNTOF(player->old_y));

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		for (uint j = 0; j < COUNTOF(player->old_x); ++j)
		{
			player[i].old_x[j] = player[i].x - (19 - j);
			player[i].old_y[j] = player[i].y - 18;
		}
		
		player[i].last_x_shot_move = player[i].x;
		player[i].last_y_shot_move = player[i].y;
	}
	
	JE_loadPic(VGAScreen, split_arcade_mode() ? 6 : 3, false);

	// Relocate the HUD to the new right edge when the playfield is wider
	const int hud_shift = vga_width - LEGACY_WIDTH;
	if (hud_shift > 0)
	{
		for (int y = 0; y < vga_height; ++y)
		{
			Uint8* row = (Uint8*)VGAScreen->pixels + y * VGAScreen->pitch;
			memmove(row + PLAYFIELD_WIDTH, row + LEGACY_WIDTH - HUD_WIDTH, HUD_WIDTH);
			memset(row + LEGACY_WIDTH - HUD_WIDTH, 0, hud_shift);
		}
	}

	JE_drawOptions();

	// Two-player HUD: left-align the name exactly as the one-player HUD does, only nudged off the
	// classic y76 so it sits vertically centred in its black readout instead of low against the frame.
	int nameX = HUD_X(268), nameY = 118;
	if (split_arcade_mode())
	{
		enum { BOX_Y = 71, BOX_H = 12, TEXT_H = 6 };
		nameY = BOX_Y + (BOX_H - TEXT_H) / 2;
	}
	JE_outText(VGAScreen, nameX, nameY, levelName, 12, 4);
	JE_drawPlayerTags();

	// Ensure the widened playfield blends into the HUD before the fade-in
	// so no remnants of the old HUD position appear during level start.
	extend_playfield_right_column(VGAScreen);

	JE_showVGA();
	JE_gammaCorrect(&colors, gammaCorrection);
	fade_palette(colors, 50, 0, 255);

	if (explosionSpriteSheet.data == NULL)
		JE_loadCompShapes(&explosionSpriteSheet, '6');

	/* MAPX will already be set correctly */
	mapY = 300 - 8;
	mapY2 = 600 - 8;
	mapY3 = 600 - 8;
	mapYPos = &megaData1.mainmap[mapY][0] - 1;
	mapY2Pos = &megaData2.mainmap[mapY2][0] - 1;
	mapY3Pos = &megaData3.mainmap[mapY3][0] - 1;
	mapXPos = 0;
	mapXOfs = 0;
	mapX2Pos = 0;
	mapX3Pos = 0;
	mapX3Ofs = 0;
	mapXbpPos = 0;
	mapX2bpPos = 0;
	mapX3bpPos = 0;

	map1YDelay = 1;
	map1YDelayMax = 1;
	map2YDelay = 1;
	map2YDelayMax = 1;

	musicFade = false;

	backPos = 0;
	backPos2 = 0;
	backPos3 = 0;
	power = 0;
	starfield_speed = 1;

	/* Setup player ship graphics */
	JE_getShipInfo();

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		player[i].x_velocity = 0;
		player[i].y_velocity = 0;

		player[i].invulnerable_ticks = 100;
	}

	newkey = newmouse = false;

	/* Initialize Level Data and Debug Mode */
	levelEnd = 255;
	levelEndWarp = -4;
	levelEndFxWait = 0;
	warningCol = 120;
	warningColChange = 1;
	warningSoundDelay = 0;
	armorShipDelay = 50;

	bonusLevel = false;
	readyToEndLevel = false;
	firstGameOver = true;
	eventLoc = 1;
	curLoc = 0;
	eventScrollCatchupValid = false;
	eventScrollSkyValid = false;
	backMove = 1;
	backMove2 = 2;
	backMove3 = 3;
	explodeMove = 2;
	enemiesActive = true;
	for (temp = 0; temp < 3; temp++)
	{
		button[temp] = false;
	}
	stopBackgrounds = false;
	stopBackgroundNum = 0;
	mapStopStallTicks = 0;
	background3x1   = false;
	background3x1b  = false;
	background3over = 0;
	background2over = 1;
	topEnemyOver = false;
	skyEnemyOverAll = false;
	smallEnemyAdjust = false;
	starActive = true;
	enemyContinualDamage = false;
	levelEnemyFrequency = 96;
	quitRequested = false;

	for (unsigned int i = 0; i < COUNTOF(boss_bar); i++)
		boss_bar[i].link_num = 0;

	memset(enemyArmedFlashAt, 0, sizeof(enemyArmedFlashAt));

	forceEvents = false;  /*Force events to continue if background movement = 0*/

	superEnemy254Jump = 0;   /*When Enemy with PL 254 dies*/

	/* Filter Status */
	filterActive = true;
	filterFade = true;
	filterFadeStart = false;
	levelFilter = -99;
	levelBrightness = -14;
	levelBrightnessChg = 1;
	flareOwnsFilter = false;  // this one is the level's fade-in, so Special Tint doesn't touch it

	background2notTransparent = false;

	uint old_weapon_bar[2] = { 0, 0 };  // only redrawn when they change

	/* Initially erase power bars */
	lastPower = power / 10;

	/* Initial Text */
	JE_drawTextWindow(miscText[20]);

	/* Setup Armor/Shield Data */
	shieldWait = 1;
	shieldT    = shields[player[0].items.shield].tpwr * 20;

	// Endless SHIELDLESS/DEADGEN sectors never recharge the shield, so hand it over fully charged:
	// you get the whole buffer up front, then fly on armor once it's spent (no way to earn it back).
	const bool startShieldFull = endlessShieldRegenOff();

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		// Start at half of the Arcade lives-scaled ceiling, which is
		// just the shield item's own mpwr*2 outside the arcade modes.
		player[i].shield_max = arcade_shield_max(&player[i]);
		player[i].shield     = startShieldFull ? player[i].shield_max : player[i].shield_max / 2;
		shieldGaugeFlash[i] = armorGaugeFlash[i] = 0;
	}
	JE_resetGaugeRender();  // nothing to interpolate away from on the level's first frames

	JE_drawShield();
	JE_drawArmor();

	// Endless keeps its bought bombs across levels (like cash/armor/perks); campaign resets each level.
	if (!endlessMode)
		for (uint i = 0; i < COUNTOF(player); ++i)
			player[i].superbombs = 0;

	/* Set cubes to 0 */
	cubeMax = 0;

	/* Secret Level Display */
	flash = 0;
	flashChange = 1;
	displayTime = 0;

	play_song(levelSong - 1);

	JE_drawPortConfigButtons();

	/* --- MAIN LOOP --- */

	newkey = false;

#ifdef WITH_NETWORK
	if (isNetworkGame)
	{
		JE_clearSpecialRequests();
		mt_srand(32402394);
	}
#endif

	// Use a fixed RNG seed for demo recording and playback so the input stream is reproducible.
	if (play_demo || record_demo)
		mt_srand(32402394);

	initialize_starfield();

	JE_setNewGameSpeed();

	set_volume(tyrMusicVolume, fxVolume);

	/*Save backup game*/
	// Skip this mid-level autosave point for endless: its continue-slot autosave lives at the
	// outpost instead, the one coherent resume point.
	if (!play_demo && !doNotSaveBackup && !timedBattleMode && !endlessMode)
	{
		temp = backup_save_slot();
		JE_saveGame(temp, "LAST LEVEL    ");   // not in endless mode: drops any stale endless half of the slot

#ifdef WITH_NETWORK
		// The backup just written is what the disconnect dialog offers to keep (network.c).
		if (isNetworkGame)
			network_session_saveable = true;
#endif
	}

	if (!play_demo && record_demo)
	{
		Uint8 new_demo_num = 0;

		do
		{
			sprintf(tempStr, "demorec.%d", new_demo_num++);
		} while (dir_file_exists(get_user_directory(), tempStr)); // until file doesn't exist

		demo_file = dir_fopen_warn(get_user_directory(), tempStr, "wb");
		if (!demo_file)
			exit(1);

		fwrite_u8_die(&episodeNum, 1, demo_file);

		// Pad string buffer with NULs.
		for (size_t i = 1; i < 10; ++i)
			if (levelName[i - 1] == '\0')
				levelName[i] = '\0';
		fwrite_u8_die((Uint8 *)levelName, 10, demo_file);

		fwrite_u8_die(&lvlFileNum, 1, demo_file);

		fwrite_u8_die(&player[0].items.weapon[FRONT_WEAPON].id,  1, demo_file);
		fwrite_u8_die(&player[0].items.weapon[REAR_WEAPON].id,   1, demo_file);
		fwrite_u8_die(&player[0].items.super_arcade_mode,        1, demo_file);
		fwrite_u8_die(&player[0].items.sidekick[LEFT_SIDEKICK],  1, demo_file);
		fwrite_u8_die(&player[0].items.sidekick[RIGHT_SIDEKICK], 1, demo_file);
		fwrite_u8_die(&player[0].items.generator,                1, demo_file);

		fwrite_u8_die(&player[0].items.sidekick_level,           1, demo_file);
		fwrite_u8_die(&player[0].items.sidekick_series,          1, demo_file);

		fwrite_u8_die(&initial_episode_num,                      1, demo_file);

		fwrite_u8_die(&player[0].items.shield,                   1, demo_file);
		fwrite_u8_die(&player[0].items.special,                  1, demo_file);
		fwrite_u8_die(&player[0].items.ship,                     1, demo_file);

		for (uint i = 0; i < 2; ++i)
			fwrite_u8_die(&player[0].items.weapon[i].power,      1, demo_file);

		Uint8 unused[3] = { 0, 0, 0 };
		fwrite_u8_die(unused, 3, demo_file);

		fwrite_u8_die(&levelSong, 1, demo_file);

		demo_keys = 0;
		demo_keys_wait = 0;
	}

	twoPlayerLinked = false;
	link_cue_state = false;
	deathSkipArmed = false;
	deathGameOverTicks = 0;
	linkGunDirec = M_PI;

	for (uint i = 0; i < COUNTOF(player); ++i)
		calc_purple_balls_needed(&player[i]);

	damageRate = 2;  /*Normal Rate for Collision Damage*/

	chargeWait   = 5;
	chargeLevel  = 0;
	chargeMax    = 5;
	chargeGr     = 0;
	chargeGrWait = 3;

	portConfigChange = false;

	/*Destruction Ratio*/
	totalEnemy = 0;
	enemyKilled = 0;

	// A chain wave spends a tick per hop, so one can still be in the air when a level ends. Drop it
	// here rather than let it fire into the next level's enemies from last level's coordinates.
	chain_reset_queue();

	superArcadePowerUp = 1;

	yourInGameMenuRequest = false;

	constantLastX = -1;

	for (uint i = 0; i < COUNTOF(player); ++i)
		player[i].exploding_ticks = 0;

	if (isNetworkGame)
	{
		JE_loadItemDat();
	}

	// After the last thing that can rewrite enemyDat, so the ball pools match this episode.
	JE_buildArcadeBallPools();

	memset(enemyAvail,       1, sizeof(enemyAvail));
	for (uint i = 0; i < COUNTOF(enemyShotAvail); i++)
		enemyShotAvail[i] = 1;

	/*Initialize Shots*/
	memset(playerShotData,   0, sizeof(playerShotData));
	memset(shotAvail,        0, sizeof(shotAvail));
	memset(shotMultiPos,     0, sizeof(shotMultiPos));
	memset(shotRepeat,       1, sizeof(shotRepeat));

	memset(button,           0, sizeof(button));

	memset(globalFlags,      0, sizeof(globalFlags));

	memset(explosions,       0, sizeof(explosions));
	memset(rep_explosions,   0, sizeof(rep_explosions));

	/* --- Clear Sound Queue --- */
	memset(soundQueue,       0, sizeof(soundQueue));
	soundQueue[3] = V_GOOD_LUCK;

	memset(enemySpriteSheetIds, 0, sizeof(enemySpriteSheetIds));
	memset(enemy,               0, sizeof(enemy));

	if (endlessMode)
		endlessPreloadBanks();  // load starting sprite banks now so early spawns aren't invisible

	// Dormant dispenser bases: the campaign obeys the Gameplay menu toggle; Endless
	// ignores it and asks the zone instead.
	dispenserBasesActive = endlessMode ? endlessDispenserBaseRoll() : restoreBaseDispensers;

	memset(SFCurrentCode,    0, sizeof(SFCurrentCode));
	memset(SFExecuted,       0, sizeof(SFExecuted));

	JE_resetSpecialState();
	hud_special_light_reset();  // the meter starts this level fresh, not mid-recharge from the last
	if (dual_ship_mode())
		coop_ship_runtime_reset();
	for (uint i = 0; i < 2; i++)  /*Launch the Attachments!*/
	{
		optionAttachmentMove[i]   = 0;
		optionAttachmentLinked[i] = true;
		optionAttachmentReturn[i] = false;
	}

	editShip1 = false;
	editShip2 = false;

	memset(smoothies, 0, sizeof(smoothies));

	levelTimer = false;
	randomExplosions = false;

	JE_resetSP();

	returnActive = false;

	galagaShotFreq = 0;

	if (galagaMode)
	{
		difficultyLevel = DIFFICULTY_NORMAL;
	}
	galagaLife = 10000;

	JE_drawOptionLevel();

	// keeps map from scrolling past the top
	BKwrap1 = BKwrap1to = &megaData1.mainmap[1][0];
	BKwrap2 = BKwrap2to = &megaData2.mainmap[1][0];
	BKwrap3 = BKwrap3to = &megaData3.mainmap[1][0];

#ifdef WITH_NETWORK
	// Rendezvous after loading so both peers begin the simulation-driven fade together.
	if (isNetworkGame)
	{
		if (qa_net_gameplay_ticks > 0)
		{
			fprintf(stderr, "net gameplay: level rendezvous\n");
			fflush(stderr);
		}

		SDL_Surface *const save_surface = VGAScreen;
		VGAScreen = VGAScreenSeg;
		network_level_loaded_rendezvous();
		VGAScreen = save_surface;
	}
#endif

	// Rollback machinery for this level: registers/allocates on first use, arms
	// the self-test in single player, resets the netplay input history.  Ship
	// spawn positions are final here, which the prediction seed relies on.
	rollback_level_start();
	// The per-tick ship snapshot is per LEVEL: left standing, the first tick of level 2
	// measured its delta against level 1's final ship position and handed every
	// ship-tracking shot fired that tick a bogus inherited velocity.
	ship_pred_have_tick = false;
	memset(ship_vel_x, 0, sizeof(ship_vel_x));
	memset(ship_vel_y, 0, sizeof(ship_vel_y));
	for (int p = 0; p < 2; ++p)
	{
		ship_tick_x[p] = player[p].x;
		ship_tick_y[p] = player[p].y;
	}
#ifdef WITH_NETWORK
	if (isNetworkGame && nrb_active())
		nrb_level_reset();
#endif

level_loop:

	// Frame boundary: snapshot this frame's pre-tick state (netplay rollback and
	// self-test), then apply the previous frame's sim-affecting request bits.
	// Runs on re-simulation passes too; that application is part of the frame.
#ifdef WITH_NETWORK
	nrb_frame_begin();
#endif
	rollback_selftest_frame_begin();

	// New sim pass: advance the enemy velocity-hint generation (blit_enemy). The presentation
	// clock skips the silent re-simulation passes, so it counts presented frames instead.
	++rl_enemy_gen;
	if (!rollback_resim_silent)
		++rl_present_gen;

	// Open the spark ring's pass here, ahead of the first spawn, so an abandoned pass can hand
	// its slots back.
	JE_beginSPPass();

	JE_deriveStarShowSpecial();

	/*Background Wrapping*/
	// Preserve whole-row overshoot when boosted scroll crosses a wrap; stop wraps remain pinned.
	if (mapYPos <= BKwrap1)
		mapYPos = (BKwrap1to > BKwrap1) ? BKwrap1to - (BKwrap1 - mapYPos) / 14 * 14 : BKwrap1to;
	if (mapY2Pos <= BKwrap2)
		mapY2Pos = (BKwrap2to > BKwrap2) ? BKwrap2to - (BKwrap2 - mapY2Pos) / 14 * 14 : BKwrap2to;
	if (mapY3Pos <= BKwrap3)
		mapY3Pos = (BKwrap3to > BKwrap3) ? BKwrap3to - (BKwrap3 - mapY3Pos) / 15 * 15 : BKwrap3to;

	allPlayersGone = all_players_dead() &&
	                 ((*player[0].lives == 1 && player[0].exploding_ticks == 0) || !arcade_rules_active()) &&
	                 ((*player[1].lives == 1 && player[1].exploding_ticks == 0) || !arcade_rules_active());

	/* Music fade. */
	if (musicFade)
	{
		if (tempVolume > 10)
		{
			tempVolume--;
			set_volume(tempVolume, fxVolume);
		}
		else
		{
			musicFade = false;
		}
	}

	if (!allPlayersGone && levelEnd > 0 && endLevel)
	{
		play_song(9);
		if (musicFade)  // cancelling the ramp isn't enough: the jingle needs the volume back too
		{
			musicFade = false;
			set_volume(tyrMusicVolume, fxVolume);
		}
	}
	else if (!playing && firstGameOver)
	{
		// Endless rolls a random per-zone track; one-shot songs otherwise just stop. Force the
		// rolled track to loop when it ends (play_song is idempotent for the current song, so it
		// won't restart it). Event 35 moves song_playing off levelSong, so its songs stay vanilla.
		if (endlessMode && song_playing == (unsigned int)(levelSong - 1))
			restart_song();
		else
			play_song(levelSong - 1);
	}

	if (!endLevel) // draw HUD
	{
		VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

		extend_playfield_right_column(VGAScreenSeg);

		/* Message bar. */
		// Live passes only: a re-simulated tick already counted down on its first
		// pass, and blit_sprite is a no-op in silent passes; a 1->0 crossing
		// landing there would swallow the erase and leave the bar poisoned with
		// stale glyphs the next message then draws over.
		if (!rollback_resim && textErase > 0 && --textErase == 0)
			blit_sprite(VGAScreenSeg, 16, 189, OPTION_SHAPES, 36);  // in-game message area

		/* Shield generator. */
		if (galagaMode)
		{
			for (uint i = 0; i < COUNTOF(player); ++i)
				player[i].shield = 0;

			// Disable two-player mode if the spawned Dragonwing died. Co-op's second ship
			// is a player with the normal death handling, not a spawned wing.
			if (!coop_mode_active() && (*player[1].lives == 0 || player[1].armor == 0))
				twoPlayerMode = false;

			if (player[0].cash >= galagaLife)
			{
				soundQueue[6] = S_EXPLOSION_11;
				soundQueue[7] = S_SOUL_OF_ZINGLON;

				if (*player[0].lives < ARCADE_LIVES_MAX)
				{
					++(*player[0].lives);
					arcade_rescale_to_lives(&player[0]);
				}
				else
					player_add_cash(&player[0], 1000);

				if (galagaLife == 10000)
					galagaLife = 20000;
				else
					galagaLife += 25000;
			}
		}
		else // not galagaMode
		{
			if (dual_ship_mode())
			{
				// Each ship recharges off its own generator. The sector modifiers reach both ships;
				// the Shield Matrix perk and the Static Discharge lockout are each ship's own, so
				// the loop names the ship it is computing. Separate arcade rides the same path; its
				// generators sit pinned at full.
				const bool regenOff  = endlessShieldRegenOff();
				const bool regenFree = endlessShieldRegenFree();
				const uint fxSaved = endlessFxPlayer();
				bool shield_changed = false;
				for (uint i = 0; i < COUNTOF(player); ++i)
				{
					Player *const this_player = &player[i];
					endlessSetFxPlayer(i);
					const int regen_cost = regenFree ? 0 : shields[this_player->items.shield].tpwr * 20;
					const uint add = endlessGeneratorPowerAdd(this_player->generator_power_add);
					this_player->generator_power = (Uint16)MIN(900u, this_player->generator_power + add);

					if (!regenOff && this_player->is_alive && this_player->shield < this_player->shield_max &&
					    this_player->generator_power > regen_cost && --this_player->shield_wait == 0)
					{
						this_player->shield_wait = (Uint8)endlessPerkShieldWait(15);
						this_player->generator_power -= (Uint16)regen_cost;
						++this_player->shield;
						shield_changed = true;
					}
				}
				endlessSetFxPlayer(fxSaved);
				if (shield_changed)
					JE_drawShield();
			}
			else if (twoPlayerMode)
			{
				if (--shieldWait == 0)
				{
					shieldWait = endlessPerkShieldWait(15);  // Shield Matrix perk shortens this in endless (no-op otherwise)

					for (uint i = 0; i < COUNTOF(player); ++i)
					{
						if (player[i].shield < player[i].shield_max && player[i].is_alive)
							++player[i].shield;
					}

					JE_drawShield();
				}
			}
			// endless AUXILIARY REACTOR: the recharge is free this sector, so the generator no longer
			// gates it either; an empty reserve must not stall a recharge that costs nothing.
			else if (player[0].is_alive && player[0].shield < player[0].shield_max
			         && (power > shieldT || endlessShieldRegenFree())
			         && !endlessShieldRegenOff())  // endless SHIELDLESS / DEADGEN: shields never recharge
			{
				if (--shieldWait == 0)
				{
					shieldWait = endlessPerkShieldWait(15);  // Shield Matrix perk shortens this in endless (no-op otherwise)

					if (!endlessShieldRegenFree())
						power -= shieldT;

					++player[0].shield;
					if (player[1].shield < player[0].shield_max)
						++player[1].shield;

					JE_drawShield();
				}
			}
		}

		/* Weapon display. */
		// Eleven gradient slots show effective weapon power. In Arcade this also tracks lives
		// and any rear-gun scaling; the wider two-player bar keeps the same slot count.
		for (uint i = 0; i < 2; ++i)
		{
			const uint hud_player = gameplay_local_player_index();
			uint item_power = arcade_weapon_power(&player[dual_ship_mode() ? hud_player : (twoPlayerMode ? i : 0)], i);

			if (old_weapon_bar[i] != item_power)
			{
				old_weapon_bar[i] = item_power;

				enum { WEAPON_BAR_SLOTS = 11 };
				const int width  = split_arcade_mode() ? 28 : WEAPON_BAR_SLOTS * 2,
				          height = split_arcade_mode() ? 3 : 2;  // rows y .. y + height

				const int x = HUD_X(split_arcade_mode() ? 284 : 289),
				          y = (i == 0) ? (split_arcade_mode() ? 5 : 17) : (split_arcade_mode() ? 99 : 38);

				fill_rectangle_xy(VGAScreenSeg, x, y, x + width - 1, y + height, 0);

				if (item_power > WEAPON_BAR_SLOTS)
					item_power = WEAPON_BAR_SLOTS;

				// Filled, not JE_rectangle: the classic slot was 2px wide by 3 rows, so an
				// outline covered every pixel it had. A 3px-wide slot has an interior, and
				// an outline would leave it black.
				for (uint j = 1; j <= item_power; ++j)
				{
					fill_rectangle_xy(VGAScreen, x + (int)(width * (j - 1)) / WEAPON_BAR_SLOTS, y,
					                             x + (int)(width * j) / WEAPON_BAR_SLOTS - 1, y + height, 115 + j); /* SEGa000 */
				}
			}
		}

		/* Power bar. */
		if (arcade_rules_active())
		{
			power = 900;
			power_gauge_active = false;

			// Every session that swaps per-ship generators through the movement pass needs
			// the classic infinite-power rule pushed into those copies too: Separate arcade
			// and the co-op ENGAGE mini-games alike, or the ships drain dry and stop firing.
			if (dual_ship_mode())
				for (uint p = 0; p < COUNTOF(player); ++p)
					player[p].generator_power = 900;

			// One-player Arcade modes use the unused generator gauge for lives; two-ship
			// sessions show this machine's own ship.
			if (dual_ship_mode() || onePlayerAction)
			{
				const uint ship = dual_ship_mode() ? gameplay_local_player_index() : 0;
				draw_lives_gauge(player_is_out(ship) ? 0 : (int)*player[ship].lives);
			}
		}
		else if (coop_mode_active())
		{
			const uint hud_player = gameplay_local_player_index();
			power = player[hud_player].generator_power;
			powerAdd = player[hud_player].generator_power_add;
			power_render_prev = power_render_cur;
			power_render_cur = (int)power;
			salvo_render_prev = salvo_render_cur;
			salvo_render_cur = local_salvo_gauge_percent();
			power_gauge_active = true;
			lastPower = power / 10;
			draw_power_gauge(VGAScreenSeg, 1, (float)power, salvo_render_cur / 100.0f);
		}
		else
		{
			// Dead Generator throttles the normal charge rate.
			power += endlessGeneratorPowerAdd(powerAdd);
			if (power > 900)
				power = 900;

			// Track prev/cur tick levels for the present loop to interpolate between;
			// draw now so the non-interpolated path still updates each tick.
			power_render_prev = power_render_cur;
			power_render_cur = (int)power;
			salvo_render_prev = salvo_render_cur;
			salvo_render_cur = local_salvo_gauge_percent();
			power_gauge_active = true;
			lastPower = power / 10;  // keep the legacy counter consistent

			draw_power_gauge(VGAScreenSeg, 1, (float)power, salvo_render_cur / 100.0f);
		}

		oldMapX3Ofs = mapX3Ofs;
		oldMapX3Ofs_f = mapX3Ofs_f;  // matching un-floored mirror (see backgrnd.c)

		enemyOnScreen = 0;
	}

	/* use game_screen for all the generic drawing functions */
	VGAScreen = game_screen;

	// Begin capturing this tick's playfield draws into the render list so they
	// can be replayed (interpolated) for in-between frames at the display rate.
	// Silent rollback re-simulation passes are never presented, so they record
	// nothing; the pass that IS presented records normally.
	if (!rollback_resim_silent)
		rl_begin_record();

	// Cleared each tick; JE_doSpecialShot re-sets a ship's while its Zinglon blast is live.
	zinglonPillarActive[0] = zinglonPillarActive[1] = false;

	/* Events. */
	while (eventRec[eventLoc-1].eventtime <= curLoc && eventLoc <= maxEvent)
		JE_eventSystem();

	// Lockstep's early exit.  Rollback mode must NOT take it: a predicted (not
	// yet confirmed) level end can still be rolled back, so the driver at the
	// end of the tick decides when the exit is real.
	if (isNetworkGame && reallyEndLevel && !nrb_active())
		goto start_level;

	/* SMOOTHIES! */
	JE_checkSmoothies();
	if (anySmoothies)
		VGAScreen = VGAScreen2;  // Smoothie effects use the secondary buffer.

	/* Backgrounds. */
	/* --- BACKGROUND 1 --- */

	// A boosted scroll can advance more than one tile (28px) per tick, so widen the render
	// list's bottom interpolation margin or its up-shift uncovers a strip below the playfield.
	// Set once per tick (before any layer draws) so all three layers agree; 3 rows cover ~96px/tick.
	bgMarginRows = (endlessFxActive() && endlessScrollBoostActive()) ? 3 : 1;

	if (forceEvents && !backMove)
		curLoc++;
	// Any forceEvents-only increment is a script-timeline step, not terrain movement. Start the
	// catch-up interval after it so a later spawn is never shifted by a stationary background.
	const int eventScrollStartThisTick = (int)curLoc;

	if (map1YDelayMax > 1 && backMove < 2)
		backMove = (map1YDelay == 1) ? 1 : 0;

	/*Draw background*/
	if (astralDuration == 0)
	{
		draw_background_1(VGAScreen);
	}
	else
	{
		// Astral Zone blanks layer 1, but its pan phase still has to be published: layer 3 pans
		// from that anchor whenever background3x1 welds the two (SURFACE), so leaving it stale
		// silently moved the over-ship terrain a tick of parallax off every other layer.
		JE_clr256(VGAScreen);
		bg_publish_layer_1_phase();
	}

	/*Set Movement of background 1*/
	// base1ScrollPx: px layer 1 (and the event pointer) actually scrolled this tick; 0 on a
	// delay-gated "off" tick (map1YDelayMax > 1). Feeds the smooth overclock boost below so it
	// fills the off-ticks instead of pulsing with the (0-on-off-ticks) instantaneous backMove.
	int base1ScrollPx = 0;
	if (--map1YDelay == 0)
	{
		map1YDelay = map1YDelayMax;

		curLoc += backMove;

		backPos += backMove;
		base1ScrollPx = backMove;

		if (backPos > 27)
		{
			backPos -= 28;
			mapY--;
			mapYPos -= 14;  /*Map Width*/
		}
	}

	// Publish the PREVIOUS tick's smooth vertical scroll rate + sub-pixel fraction for the render
	// list (the present loop shows this tick's list at its pre-advance position, so the data lags
	// one tick, matching bgScrollDeltaY).
	for (int L = 1; L <= 3; ++L)
	{
		bg_layer_dy[L]    = bgSmoothRatePend[L];
		bg_layer_yfrac[L] = bgSmoothFracPend[L];
	}
	bg_smooth_y_active = bgSmoothActivePend;

	// Smooth every layer to its true average scroll rate so a delay-gated slow section (event 3:
	// layer 1 1px/3 ticks, layer 2 1px/2 ticks) slides sub-pixel instead of freezing then jumping.
	{
		int fire1 = (map1YDelayMax > 1 && backMove < 2) ? 1 : (int)backMove;
		endlessScrollExtraPx1 = endlessScrollExtraPx(0, fire1, map1YDelayMax, base1ScrollPx,
		                                             &bgSmoothRatePend[1], &bgSmoothFracPend[1]);
		eventScrollBaseStep[1] = fire1;
		eventScrollDelayMax[1] = map1YDelayMax;

		int fire2 = (map2YDelayMax > 1 && backMove2 < 2) ? 1 : (int)backMove2;
		int base2 = (map2YDelay == 1) ? fire2 : 0;
		endlessScrollExtraPx2 = endlessScrollExtraPx(1, fire2, map2YDelayMax, base2,
		                                             &bgSmoothRatePend[2], &bgSmoothFracPend[2]);

		// Sky-glue spawn anchor: stock layer ratio + the layers' current carry phase (the
		// fracs are these exact integer hundredths). Captured post-update, so events at the
		// START of the next tick see the state their crossed interval ended on.
		eventScrollSkyValid = fire1 > 0 && fire2 > 0;
		if (eventScrollSkyValid)
		{
			eventScrollSkyRatio100 = 100 * fire2 * map1YDelayMax / (fire1 * map2YDelayMax);
			const int c1 = (int)lroundf(bgSmoothFracPend[1] * 100.0f);
			const int c2 = (int)lroundf(bgSmoothFracPend[2] * 100.0f);
			eventScrollSkyPhase100 = eventScrollSkyRatio100 * c1 / 100 - c2;
		}

		endlessScrollExtraPx3 = endlessScrollExtraPx(2, (int)backMove3, 1, (int)backMove3,
		                                             &bgSmoothRatePend[3], &bgSmoothFracPend[3]);
		eventScrollBaseStep[3] = (int)backMove3;
		eventScrollDelayMax[3] = 1;
	}
	bgSmoothActivePend = true;

	// Publish this tick's (non-lagged) rate + fraction for background layer 3, which advances before
	// it records its rows (unlike layers 1/2). Enemy banks preserve their common pre-advance phase
	// and use the lagged bg_layer_dy/bg_layer_yfrac values even when bound to layer 3.
	for (int L = 1; L <= 3; ++L)
	{
		bg_layer_yfrac_now[L] = bgSmoothFracPend[L];
		bg_layer_dy_now[L]    = bgSmoothRatePend[L];
	}

	// Layer 1 (+ the event pointer, which must ride the identical delta so scripted stops stay
	// aligned to the terrain). One combined advance; the wrap can cross more than one tile.
	curLoc += endlessScrollExtraPx1;
	eventScrollFrom = eventScrollStartThisTick;
	eventScrollTo = (int)curLoc;
	eventScrollLayerDelta[1] = eventScrollTo - eventScrollFrom;
	eventScrollLayerDelta[3] = (int)backMove3 + endlessScrollExtraPx3;
	eventScrollBoost = endlessScrollBoostPercent();
	eventScrollCatchupValid = eventScrollBoost > 0 && eventScrollTo > eventScrollFrom;
	backPos += endlessScrollExtraPx1;
	while (backPos > 27)
	{
		backPos -= 28;
		mapY--;
		mapYPos -= 14;
	}

	if (starActive || astralDuration > 0)
		update_and_draw_starfield(VGAScreen, starfield_speed);

	if (processorType > 1 && smoothies[5-1])
	{
		if (render_list_recording)
			rl_rec_smoothie_filter(RC_ICED_BLUR);
		iced_blur_filter(game_screen, VGAScreen);
		VGAScreen = game_screen;
	}

	/* Background layer 2. */
	if (background2over == 3)
	{
		draw_background_2(VGAScreen);
		background2 = true;
	}

	if (background2over == 0)
	{
		if (!(smoothies[2-1] && processorType < 4) && !(smoothies[1-1] && processorType == 3))
		{
			if (wild && !background2notTransparent)
				draw_background_2_blend(VGAScreen);
			else
				draw_background_2(VGAScreen);
		}
	}

	if (smoothies[0] && processorType > 2 && smoothie_data[0] == 0)
	{
		if (render_list_recording)
			rl_rec_smoothie_filter(RC_LAVA_FILTER);
		lava_filter(game_screen, VGAScreen);
		VGAScreen = game_screen;
	}
	if (smoothies[2-1] && processorType > 2)
	{
		if (render_list_recording)
			rl_rec_smoothie_filter(RC_WATER_FILTER);
		water_filter(game_screen, VGAScreen);
		VGAScreen = game_screen;
	}

	endlessGroupHoming();

	/* Ground enemies. */
	lastEnemyOnScreen = enemyOnScreen;

	tempMapXOfs = mapXOfs + PLAYFIELD_X_SHIFT;
	tempMapXOfs_frac = mapXOfs_f - mapXOfs;
	tempMapXOfs_layer = 1;
	tempBackMove = backMove;
	tempScrollExtraPx  = endlessScrollExtraPx1;  // this batch rides layer 1
	tempScrollYLayer   = 1;
	tempScrollBaseStep = (map1YDelayMax > 1 && backMove < 2) ? 1 : (int)backMove;
	tempScrollDelayMax = map1YDelayMax;
	// Layer 1 records both terrain and enemy sprites before the ey advance, so their
	// fractional phases match. Health bars are drawn after the advance; pull their
	// integer anchor back by the same amount and retain the pre-advance phase.
	tempScrollYBase    = 0;
	tempScrollYfrac    = bg_layer_yfrac[1];
	tempScrollYBaseBar = -(tempBackMove + tempScrollExtraPx);
	tempScrollYfracBar = bg_layer_yfrac[1];
	JE_drawEnemy(50);
	JE_drawEnemy(100);

	if (enemyOnScreen == 0 || enemyOnScreen == lastEnemyOnScreen)
	{
		if (stopBackgroundNum == 1)
			stopBackgroundNum = 9;
	}

	if (smoothies[0] && processorType > 2 && smoothie_data[0] > 0)
	{
		if (render_list_recording)
			rl_rec_smoothie_filter(RC_LAVA_FILTER);
		lava_filter(game_screen, VGAScreen);
		VGAScreen = game_screen;
	}

	if (superWild)
	{
		neat += 3;
		JE_darkenBackground(neat);
	}

	/* Background layer 2. */
	if (!(smoothies[2-1] && processorType < 4) &&
	    !(smoothies[1-1] && processorType == 3))
	{
		if (background2over == 1)
		{
			if (wild && !background2notTransparent)
				draw_background_2_blend(VGAScreen);
			else
				draw_background_2(VGAScreen);
		}
	}

	if (superWild)
	{
		neat++;
		JE_darkenBackground(neat);
	}

	if (background3over == 2)
		draw_background_3(VGAScreen);

	/* New Enemy */
	if (enemiesActive && levelEnemyMax > 0 && mt_rand() % 100 > levelEnemyFrequency)
	{
		tempW = levelEnemy[mt_rand() % levelEnemyMax];
		if (tempW == 2)
			soundQueue[3] = S_WEAPON_7;
		b = JE_newEnemy(0, tempW, 0);
	}

	if (processorType > 1 && smoothies[3-1])
	{
		if (render_list_recording)
			rl_rec_smoothie_filter(RC_ICED_BLUR);
		iced_blur_filter(game_screen, VGAScreen);
		VGAScreen = game_screen;
	}
	if (processorType > 1 && smoothies[4-1])
	{
		if (render_list_recording)
			rl_rec_smoothie_filter(RC_BLUR);
		blur_filter(game_screen, VGAScreen);
		VGAScreen = game_screen;
	}

	/* Draw Sky Enemy */
	if (!skyEnemyOverAll)
	{
		lastEnemyOnScreen = enemyOnScreen;

		tempMapXOfs = mapX2Ofs + PLAYFIELD_X_SHIFT;
		tempMapXOfs_frac = mapX2Ofs_f - mapX2Ofs;
		tempMapXOfs_layer = 2;
		tempBackMove = 0;
		tempScrollExtraPx  = 0;     // layer-2 anchor: any ride is per-enemy via skyGlueThisEnemy
		tempScrollYLayer   = 0;
		tempScrollBaseStep = 0;
		tempScrollDelayMax = 1;
		tempScrollYBase = tempScrollYBaseBar = 0;
		tempScrollYfrac = tempScrollYfracBar = 0.0f;
		JE_drawEnemy(25);

		if (enemyOnScreen == lastEnemyOnScreen)
		{
			if (stopBackgroundNum == 2)
				stopBackgroundNum = 9;
		}
	}

	if (background3over == 0)
		draw_background_3(VGAScreen);

	/* Draw Top Enemy */
	if (!topEnemyOver)
	{
		tempMapXOfs = ((background3x1 == 0) ? oldMapX3Ofs : mapXOfs) + PLAYFIELD_X_SHIFT;
		tempMapXOfs_frac = (background3x1 == 0) ? (oldMapX3Ofs_f - oldMapX3Ofs) : (mapXOfs_f - mapXOfs);
		tempMapXOfs_layer = 3;
		tempBackMove = backMove3;
		tempScrollExtraPx  = endlessScrollExtraPx3;  // this batch rides layer 3
		tempScrollYLayer   = 3;
		tempScrollBaseStep = (int)backMove3;
		tempScrollDelayMax = 1;
		// All enemies are authored and recorded at their pre-advance ey phase.
		tempScrollYBase    = 0;
		tempScrollYfrac    = bg_layer_yfrac[3];
		tempScrollYBaseBar = -(tempBackMove + tempScrollExtraPx);
		tempScrollYfracBar = bg_layer_yfrac[3];
		JE_drawEnemy(75);
	}

	// Cache whether each enemy has an opaque pixel in the visible playfield. This
	// uses collision state, not draw state, so the kill gate stays deterministic.
	bool enemyVisible[COUNTOF(enemy)];
	for (unsigned int ev = 0; ev < COUNTOF(enemy); ev++)
		enemyVisible[ev] = (enemyAvail[ev] == 0) && enemy_has_visible_pixel(ev);

	/* Player Shot Images */
	for (int z = 0; z < MAX_PWEAPON; z++)
	{
		if (shotAvail[z] != 0)
		{
			// The endless pierce lockout ages here; once per bullet per sim tick, immediately
			// before this bullet's own collision pass reads it.
			{
				PlayerShotDataType *ps = &playerShotData[z];
				if (ps->pierceLock > 0)
					--ps->pierceLock;

				// Apply last tick's pierce lock after the current hit pass. Locking during
				// the pass would stop one bullet from crossing overlapping hulls.
				if (ps->pierceLockPending > 0)
				{
					int ticks = ps->pierceLockPending / ENDLESS_PIERCE_LOCK_SCALE;
					int carry = ps->pierceLockCarry + ps->pierceLockPending % ENDLESS_PIERCE_LOCK_SCALE;
					if (carry >= ENDLESS_PIERCE_LOCK_SCALE)
					{
						carry -= ENDLESS_PIERCE_LOCK_SCALE;
						++ticks;
					}
					ps->pierceLockCarry = (JE_byte)carry;
					ps->pierceLockPending = 0;
					if (ticks > 0)
						ps->pierceLock = (JE_byte)ticks;
				}
			}

			// Zinglon uses the reserved last slot without a playerShotData entry. Its branch below
			// supplies these outputs, and safe defaults keep player indexing in range.
			bool is_special = false;
			int tempShotX = 0, tempShotY = 0;
			JE_byte chain = 0;
			JE_byte playerNum = 1;
			JE_word tempX2 = 0, tempY2 = 0;
			JE_integer damage = 0;
			int shotHitDx, shotHitDy;

			if (!player_shot_move_and_draw(z, &is_special, &tempShotX, &tempShotY, &damage, &temp2,
			                               &chain, &playerNum, &tempX2, &tempY2,
			                               &shotHitDx, &shotHitDy))
			{
				goto draw_player_shot_loop_end;
			}

			// The point this shot is collided from: the top-left corner of its sprite cell under
			// Classic, the middle of the frame it just drew under Centered Hitboxes. The enemy
			// side of the pair is worked out per enemy below.
			const int shotHitX = tempShotX + shotHitDx;
			const int shotHitY = tempShotY + shotHitDy;

			// Scale real damage from every Endless source. Decode ice and piercing markers before
			// scaling, then restore the marker.
			if (endlessFxActive() && z != MAX_PWEAPON - 1)
			{
				endlessSetFxPlayer(playerShotData[z].playerNumber >= 1
				                   ? (uint)playerShotData[z].playerNumber - 1 : 0);
				int dmgPct = endlessPlayerDamagePercent();
				// Opening Salvo perk: shots tagged as part of a charged volley get an extra bump on top.
				if (playerShotData[z].salvoBoost)
					dmgPct += endlessOpeningSalvoDamagePercent();
				if (damage >= 250)
				{
					// Piercing rounds keep their marker and carry their own remainder, so the
					// run's levers reach a 1-damage round.
					damage = 250 + endlessPierceHitDamage(damage - 250, dmgPct,
					                                      &playerShotData[z].pierceDmgCarry);
				}
				else if (dmgPct != 100 && damage != 99)   // 99 is the ice marker: nothing to scale
				{
					int scaled = (damage * dmgPct + 50) / 100;   // round, don't truncate; see below
					if (damage > 0)
					{
						if (scaled < 1)
							scaled = 1;              // a shot that deals damage never rounds away to none
						// Weapon-table damage runs as low as 1, where plain integer scaling
						// rounds most of the lever away (+50% on 2 damage buys nothing), so an
						// uplift moves the number by at least one.
						if (dmgPct > 100 && scaled <= damage)
							scaled = damage + 1;
					}
					if (scaled > 249)                // keep clear of the piercing marker...
						scaled = 249;
					else if (scaled == 99)           // ...and of the ice one
						scaled = 100;
					damage = scaled;
				}
			}

			for (b = 0; b < 100; b++)
			{
				if (enemyAvail[b] == 0)
				{
					bool collided;

					// Do not damage enemies before an opaque pixel enters the playfield.
					// enemyVisible[] is computed above from this tick's collision state.
					const bool enemyOnPlayfield = enemyVisible[b];

					// Enemy side of that pair. Its quadrants are drawn at +/-6 x, +/-7 y of the
					// anchor, so that is where the middle of its sprite sits; Classic keeps the
					// vanilla anchor and the -12 / -6 bias that goes with it.
					const int enemyHitX = enemy[b].ex + enemy[b].mapoffset + (centeredShotHitboxes ? 6 : 0);
					const int enemyHitYBig = enemy[b].ey + (centeredShotHitboxes ? 7 : -12);
					const int enemyHitYSmall = enemy[b].ey + (centeredShotHitboxes ? 7 : -6);

					if (z == MAX_PWEAPON - 1)
					{
						int width = 0, pillarDamage = 0;
						uint owner = 0;
						collided = zinglon_pillar_hit(enemy[b].ex + enemy[b].mapoffset,
						                              &width, &pillarDamage, &owner);
						temp = (JE_byte)width;
						temp2 = 9;
						chain = 0;
						damage = pillarDamage;
						// Damage perks and kill credit belong to the beam's owner.
						playerNum = (JE_byte)(owner + 1);
						endlessSetFxPlayer(owner);
					}
					else if (is_special)
					{
						collided = ((enemy[b].enemycycle == 0) &&
						            (abs(enemyHitX - shotHitX)      < (25 + tempX2)) &&
						            (abs(enemyHitYBig - shotHitY)   < (29 + tempY2))) ||
						           ((enemy[b].enemycycle > 0) &&
						            (abs(enemyHitX - shotHitX)      < (13 + tempX2)) &&
						            (abs(enemyHitYSmall - shotHitY) < (15 + tempY2)));
					}
					else
					{
						collided = ((enemy[b].enemycycle == 0) &&
						            (abs(enemyHitX - shotHitX) < 25) && (abs(enemyHitYBig - shotHitY) < 29)) ||
						           ((enemy[b].enemycycle > 0) &&
						            (abs(enemyHitX - shotHitX) < 13) && (abs(enemyHitYSmall - shotHitY) < 15));
					}

					if (collided && enemyOnPlayfield)
					{
						if (chain > 0)
						{
							// The Zinglon pseudo-shot clears chain above, so this slot always has
							// playerShotData behind it.
							shotMultiPos[SHOT_MISC] = 0;
							b = player_shot_create_chained(tempShotX, tempShotY, mouseX, mouseY, chain, playerNum,
							                               playerShotData[z].salvoBoost != 0);
							shotAvail[z] = 0;
							goto draw_player_shot_loop_end;
						}

						infiniteShot = false;

						if (damage == 99)
						{
							damage = 0;
							doIced = 40;
							enemy[b].iced = 40;
						}
						else
						{
							doIced = 0;
							if (damage >= 250)
							{
								damage = damage - 250;
								infiniteShot = true;
							}
						}

						// The damage the bullet itself carries, kept for the further hulls it
						// crosses this tick. `damage` below becomes what this hull's accumulator
						// paid out, which is zero on anything with an HP multiplier.
						// See doc/notes.md#combat.
						const int bulletDamage = damage;

						int armorleft = enemy[b].armorleft;

						const bool has_boss_bar = enemy_has_boss_bar(enemy[b].linknum);
						const int hpMult = enemy_hp_multiplier(b);
						const int divisor100 = enemy_hp_divisor100(b);

						// Per-bullet lockout prevents a piercing shot from damaging the same
						// scaled hull on every overlapping tick.
						if (endlessFxActive() && infiniteShot)
						{
							// Ask what THIS hull's tier is owed BEFORE consulting the lock, never the other way
							// round: an ordinary enemy answers 0 and so is neither charged nor blocked, even while
							// the bullet is locked out of a boss it overlaps.
							const int lock100 = endlessPierceLock100(has_boss_bar, hpMult, enemy[b].eliteState);
							if (lock100 > 0)
							{
								PlayerShotDataType *pshot = &playerShotData[z];
								if (pshot->pierceLock > 0)
								{
									damage += 250;   // re-encode: the bullet flies on, dealing nothing here
									continue;
								}
							// Bank the toughest lockout crossed this tick; apply it next pass
							// so the current sweep can hit every aligned hull.
								if (lock100 > pshot->pierceLockPending)
									pshot->pierceLockPending = (JE_byte)lock100;
							}
						}

						// Apply Executioner and Knife Fight to raw damage before the health multiplier
						// accumulator; a post-divide percentage would vanish against heavily scaled targets.
						const int knifePct = endlessFxActive()
							? endlessPerkKnifeFightPercent((unsigned)b) : 0;
						const int knifeRaw = endlessPerkKnifeFightBonus(damage, knifePct);
						const int perkRaw = endlessFxActive()
							? endlessPerkExecutionerBonus(damage, enemy[b].armorleft,
							      enemy[b].healthbar_seen ? enemy[b].healthbar_max : 0, has_boss_bar)
							  + knifeRaw
							: 0;
						if (knifeRaw > 0)
							endlessPerkKnifeFightBlood((unsigned)b, knifePct);

						// Armor points the bonus actually bought, which is what the shot-carry paths at the
						// bottom have to undo; they work in post-divide space, so the raw bonus is the
						// wrong quantity to subtract there.
						int perkBonus;

						if (divisor100 > 100)
						{
							// Run the accumulator once, then measure the perks by re-dividing the same
							// starting state without them. `plain` only reads the pre-hit accumulator; the
							// bonused pass is the one that commits.
							const int plain = (enemy[b].damageAccum
							                   + damage * ENEMY_DAMAGE_ACCUM_SCALE) / divisor100;
							enemy[b].damageAccum += (damage + perkRaw) * ENEMY_DAMAGE_ACCUM_SCALE;
							damage = enemy[b].damageAccum / divisor100;
							enemy[b].damageAccum -= damage * divisor100;
							perkBonus = damage - plain;
						}
						else
						{
							damage += perkRaw;
							perkBonus = perkRaw;
						}

						temp = enemy[b].linknum;
						if (temp == 0)
							temp = 255;

						if (enemy[b].armorleft < 255)
						{
							boss_bar_note_hit((JE_byte)temp);

							if (enemy[b].enemyground)
								enemy[b].filter = temp2;

							for (unsigned int e = 0; e < COUNTOF(enemy); e++)
							{
								if (enemy[e].linknum == temp &&
								    enemyAvail[e] != 1 &&
								    enemy[e].enemyground != 0)
								{
									if (doIced)
										enemy[e].iced = doIced;
									enemy[e].filter = temp2;
								}
							}
						}

						if (armorleft > damage)
						{
							if (z != MAX_PWEAPON - 1)
							{
								if (enemy[b].armorleft != 255)
								{
									if (!enemy[b].healthbar_seen)
									{
										// pre-hit armor, if nothing established one earlier
										enemy_note_full_armor(&enemy[b]);
										enemy[b].healthbar_seen = true;
									}
									enemy[b].armorleft -= damage;
									JE_setupExplosion(tempShotX, tempShotY, 0, 0, false, false);
								}
								else
								{
									JE_doSP(tempShotX + 6, tempShotY + 6, damage / 2 + 3, damage / 4 + 2, temp2, false);
								}
							}

							soundQueue[5] = S_ENEMY_HIT;

							if ((armorleft - damage <= enemy[b].edlevel) &&
							    ((!enemy[b].edamaged) ^ (enemy[b].edani < 0)))
							{

								for (temp3 = 0; temp3 < 100; temp3++)
								{
									if (enemyAvail[temp3] != 1)
									{
										int linknum = enemy[temp3].linknum;
										if (
										     (temp3 == b) ||
										     (
										       (temp != 255) &&
										       (
										         ((enemy[temp3].edlevel > 0) && (linknum == temp)) ||
										         (
										           (enemyContinualDamage && (temp - 100 == linknum)) ||
										           ((linknum > 40) && (linknum / 20 == temp / 20) && (linknum <= temp))
										         )
										       )
										     )
										   )
										{
											enemy[temp3].enemycycle = 1;

											enemy[temp3].edamaged = !enemy[temp3].edamaged;

											if (enemy[temp3].edani != 0)
											{
												enemy[temp3].ani = abs(enemy[temp3].edani);
												enemy[temp3].aniactive = 1;
												enemy[temp3].animax = 0;
												enemy[temp3].animin = enemy[temp3].edgr;
												enemy[temp3].enemycycle = enemy[temp3].animin - 1;

											}
											else if (enemy[temp3].edgr > 0)
											{
												enemy[temp3].egr[1-1] = enemy[temp3].edgr;
												enemy[temp3].ani = 1;
												enemy[temp3].aniactive = 0;
												enemy[temp3].animax = 0;
												enemy[temp3].animin = 1;
											}
											else
											{
												// Tally, bounty, SHOCKWAVE, MARTYRDOM and the Chain Reaction pulse all live in
												// the one helper; never inline any of them here again (see tyrian2.h).
												enemy_logical_death(temp3,
												                    playerNum >= 1 ? (int)playerNum - 1
												                                   : ENDLESS_KILLER_NONE);
											}

											enemy[temp3].aniwhenfire = 0;

											if (enemy[temp3].armorleft > (unsigned char)enemy[temp3].edlevel)
											{
												const JE_byte wasArmor = enemy[temp3].armorleft;
												enemy[temp3].armorleft = enemy[temp3].edlevel;
												enemy_note_full_armor(&enemy[temp3]);
												enemy_note_armed(temp3, wasArmor);
											}

											JE_integer tempX = enemy[temp3].ex + enemy[temp3].mapoffset;
											JE_integer tempY = enemy[temp3].ey;

											explosionFilter = endlessEliteTint(enemy[temp3].eliteState);
											if (enemyDat[enemy[temp3].enemytype].esize != 1)
												JE_setupExplosion(tempX, tempY - 6, 0, 1, false, false);
											else
												JE_setupExplosionLarge(enemy[temp3].enemyground, enemy[temp3].explonum / 2, tempX, tempY);
											explosionFilter = 0;
										}
									}
								}
							}
						}
						else
						{
							// in galaga mode player 2 is sidekick, so give cash to player 1
							enemy_kill_group(b, galagaMode ? 0 : (int)playerNum - 1,
							                 playerNum >= 1 ? (int)playerNum - 1 : ENDLESS_KILLER_NONE);
						}

						if (infiniteShot)
						{
							damage = bulletDamage + 250;
						}
						else
						{
							// Executioner and Knife Fight: undo this hull's bonus before an overkill
							// shot carries `damage` to the next enemy.
							damage -= perkBonus;

							if (z != MAX_PWEAPON - 1)
							{
								if (damage <= armorleft)
								{
									shotAvail[z] = 0;
									goto draw_player_shot_loop_end;
								}
								else
								{
									playerShotData[z].shotDmg -= armorleft;
								}
							}
						}
					}
				}
			}

draw_player_shot_loop_end:
			;
		}
	}

	// Chain Reaction perk: drain the queued death-pulses now that the player-shot loop is done, so a
	// chain kill can neither recurse nor disturb the loop's per-linkgroup kill bookkeeping.
	chain_reaction_process();

	/* Player movement indicators for shots that track your ship */
	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		player[i].last_x_shot_move = player[i].x;
		player[i].last_y_shot_move = player[i].y;
	}
	
	/* Collision detection. */
	
	for (uint i = 0; i < (twoPlayerMode ? 2 : 1); ++i)
		if (player[i].is_alive && !endLevel)
			JE_playerCollide(&player[i], i + 1);
	
	if (firstGameOver)
		JE_mainGamePlayerFunctions();      /* Player draw and movement. */

	if (!endLevel)
	{    /* Draw the active level. */

		/* Draw enemy shots. */
		for (int z = 0; z < ENEMY_SHOT_MAX; z++)
		{
			if (enemyShotAvail[z] == 0)
			{
				if (enemyShot[z].seekerArm > 0 && ++enemyShot[z].seekerArm >= ENDLESS_SEEKER_DELAY_TICKS)
				{
					endlessSeekerCorrect(&enemyShot[z]);
					enemyShot[z].seekerLeft = (enemyShot[z].seekerLeft > 0) ? enemyShot[z].seekerLeft - 1 : 0;
					enemyShot[z].seekerArm = (enemyShot[z].seekerLeft > 0) ? 1 : 0;
				}

				// Homing shots chase the nearer ship still flying; solo play keeps player 1.
				const Player *const homeTo =
					&player[endlessFxActive() ? endlessDangerTargetPlayer(enemyShot[z].sx, enemyShot[z].sy) : 0];

				enemyShot[z].sxm += enemyShot[z].sxc;
				enemyShot[z].sx += enemyShot[z].sxm;

				if (enemyShot[z].tx != 0)
				{
					if (enemyShot[z].sx > homeTo->x)
					{
						if (enemyShot[z].sxm > -enemyShot[z].tx)
							enemyShot[z].sxm--;
					}
					else
					{
						if (enemyShot[z].sxm < enemyShot[z].tx)
							enemyShot[z].sxm++;
					}
				}

				enemyShot[z].sym += enemyShot[z].syc;
				enemyShot[z].sy += enemyShot[z].sym;

				if (enemyShot[z].ty != 0)
				{
					if (enemyShot[z].sy > homeTo->y)
					{
						if (enemyShot[z].sym > -enemyShot[z].ty)
							enemyShot[z].sym--;
					}
					else
					{
						if (enemyShot[z].sym < enemyShot[z].ty)
							enemyShot[z].sym++;
					}
				}

				// X cull is against the VISIBLE window: [PLAYFIELD_LEFT, PLAYFIELD_RIGHT] after the
				// composite crop (vanilla: 0..262, cull at >275/<=0).
				if (enemyShot[z].duration-- == 0 || enemyShot[z].sy > 190 || enemyShot[z].sy <= -14 || enemyShot[z].sx > PLAYFIELD_RIGHT + 13 || enemyShot[z].sx <= 0)
				{
					enemyShotAvail[z] = true;
				}
				else  // check if shot collided with player
				{
					// Same pairing as the player-shot test above: Classic anchors the shot at the
					// top-left corner of its sprite cell and the ship box on its position, Centered
					// takes the middle of each (the hull is blitted 5 left and 7 up of its position,
					// 24 by 28, so its middle is +7/+7).
					int eShotHitDx, eShotHitDy;
					enemy_shot_hit_offset(enemyShot[z].sgr, enemyShot[z].animate, &eShotHitDx, &eShotHitDy);
					const int eShotHitX = enemyShot[z].sx + eShotHitDx;
					const int eShotHitY = enemyShot[z].sy + eShotHitDy;

					for (uint i = 0; i < (twoPlayerMode ? 2 : 1); ++i)
					{
						// endless LOW PROFILE boon shrinks the box ~25%; endlessHitboxScale returns the
						// stock extent otherwise, so this reads as the vanilla test in every other game.
						const int hitX = endlessHitboxScale((int)player[i].shot_hit_area_x);
						const int hitY = endlessHitboxScale((int)player[i].shot_hit_area_y);
						const int shipHitX = player[i].x + (centeredShotHitboxes ? 7 : 0);
						const int shipHitY = player[i].y + (centeredShotHitboxes ? 7 : 0);
						if (player[i].is_alive &&
						    eShotHitX > shipHitX - hitX &&
						    eShotHitX < shipHitX + hitX &&
						    eShotHitY > shipHitY - hitY &&
						    eShotHitY < shipHitY + hitY)
						{
							JE_integer tempX = enemyShot[z].sx;
							JE_integer tempY = enemyShot[z].sy;
							temp = enemyShot[z].sdmg;

							enemyShotAvail[z] = true;

							JE_setupExplosion(tempX, tempY, 0, 0, false, false);

							if (player[i].invulnerable_ticks == 0)
							{
								const uint shieldBefore = player[i].shield;
								const uint armorBefore = player[i].armor;
								if ((temp = JE_playerDamage(temp, &player[i])) > 0)
								{
									player[i].x_velocity += (enemyShot[z].sxm * temp) / 2;
									player[i].y_velocity += (enemyShot[z].sym * temp) / 2;
								}

								// Deflector: a hit the shield took whole flies back out along the
								// reverse of its path, as this ship's shot. JE_playerDamage left the
								// effect context on this ship, so the perk read is its own.
								if (endlessFxActive() && player[i].armor == armorBefore
								    && player[i].shield < shieldBefore)
								{
									const int absorbed = (int)(shieldBefore - player[i].shield);
									const int returned = endlessPerkDeflectDamage(absorbed);
									if (returned > 0)
										player_shot_create_deflected(&enemyShot[z], returned,
										                             (JE_byte)(i + 1));
								}

								// Refund after damage resolution so armor overflow uses the full hit.
								if (endlessFxActive() && player[i].shield < shieldBefore)
								{
									const int stopped = (int)(shieldBefore - player[i].shield);
									const int spared = endlessPerkDeflectShieldSpared(stopped);
									if (spared > 0)
									{
										player[i].shield += (uint)spared;
										hud_bars_dirty = true;  // JE_playerDamage drew the unrefunded loss
									}
								}
							}

							break;
						}
					}

					if (enemyShotAvail[z] == false)
					{
						if (enemyShot[z].animax != 0)
						{
							if (++enemyShot[z].animate >= enemyShot[z].animax)
								enemyShot[z].animate = 0;
						}

						rl_current_id = RL_ID_ESHOT_BASE + z;
						// Record the shot's real per-tick velocity AND acceleration so the
						// render list can extrapolate it smoothly (past the generic snap
						// threshold, and without a decelerating shot appearing to reverse).
						rl_current_vel_x = enemyShot[z].sxm;
						rl_current_vel_y = enemyShot[z].sym;
						rl_current_acc_x = enemyShot[z].sxc;
						rl_current_acc_y = enemyShot[z].syc;
						// Graphics from 500 up live in the second sheet, indexed from its start.
						const bool highSheet = (enemyShot[z].sgr >= 500);
						Sprite2_array *const sheet = highSheet ? &spriteSheet12 : &spriteSheet8;
						const unsigned int frame =
							enemyShot[z].sgr + enemyShot[z].animate - (highSheet ? 500 : 0);
						if (enemyShot[z].filter != 0)
							blit_sprite2_filter_bright(VGAScreen, enemyShot[z].sx, enemyShot[z].sy, *sheet,
							                           frame, enemyShot[z].filter | ENDLESS_SHOT_BRIGHT);
						else
							blit_sprite2(VGAScreen, enemyShot[z].sx, enemyShot[z].sy, *sheet, frame);
						rl_current_id = 0;
						rl_current_vel_x = 0;
						rl_current_vel_y = 0;
						rl_current_acc_x = 0;
						rl_current_acc_y = 0;
					}
				}

			}
		}
	}

	if (background3over == 1)
		draw_background_3(VGAScreen);

	/* Draw Top Enemy */
	if (topEnemyOver)
	{
		// This bank and layer 3 are both drawn after JE_mainGamePlayerFunctions updates
		// parallax. Use that same current anchor so foreground-mounted enemies (notably
		// EP1 DELIANI) do not trail the terrain by the player's variable per-tick pan.
		tempMapXOfs = ((background3x1 == 0) ? mapX3Ofs : mapXOfs) + PLAYFIELD_X_SHIFT;
		tempMapXOfs_frac = (background3x1 == 0) ? (mapX3Ofs_f - mapX3Ofs) : (mapXOfs_f - mapXOfs);
		tempMapXOfs_layer = 3;
		tempBackMove = backMove3;
		tempScrollExtraPx  = endlessScrollExtraPx3;  // this batch rides layer 3
		tempScrollYLayer   = 3;
		tempScrollBaseStep = (int)backMove3;
		tempScrollDelayMax = 1;
		// Keep every enemy bank at the common pre-advance entity phase; see the matching
		// !topEnemyOver path above. The bar is recorded after JE_drawEnemy advances ey.
		tempScrollYBase    = 0;
		tempScrollYfrac    = bg_layer_yfrac[3];
		tempScrollYBaseBar = -(tempBackMove + tempScrollExtraPx);
		tempScrollYfracBar = bg_layer_yfrac[3];
		JE_drawEnemy(75);
	}

	/* Draw Sky Enemy */
	if (skyEnemyOverAll)
	{
		lastEnemyOnScreen = enemyOnScreen;

		tempMapXOfs = mapX2Ofs + PLAYFIELD_X_SHIFT;
		tempMapXOfs_frac = mapX2Ofs_f - mapX2Ofs;
		tempMapXOfs_layer = 2;
		tempBackMove = 0;
		tempScrollExtraPx  = 0;     // layer-2 anchor: any ride is per-enemy via skyGlueThisEnemy
		tempScrollYLayer   = 0;
		tempScrollBaseStep = 0;
		tempScrollDelayMax = 1;
		tempScrollYBase = tempScrollYBaseBar = 0;
		tempScrollYfrac = tempScrollYfracBar = 0.0f;
		JE_drawEnemy(25);

		if (enemyOnScreen == lastEnemyOnScreen)
		{
			if (stopBackgroundNum == 2)
				stopBackgroundNum = 9;
		}
	}

	/* Sequenced explosions. */
	enemyStillExploding = false;
	for (int i = 0; i < MAX_REPEATING_EXPLOSIONS; i++)
	{
		if (rep_explosions[i].ttl != 0)
		{
			enemyStillExploding = true;

			if (rep_explosions[i].delay > 0)
			{
				rep_explosions[i].delay--;
				continue;
			}

			// Track layer 2 while keeping the explosion's one-pixel fall speed.
			rep_explosions[i].y += backMove2 + endlessScrollExtraPx2 + 1;
			JE_integer tempX = rep_explosions[i].x + (mt_rand() % 24) - 12;
			JE_integer tempY = rep_explosions[i].y + (mt_rand() % 27) - 24;

			// A big sequence keeps re-arming itself, so the tint has to travel with it.
			explosionFilter = rep_explosions[i].filter;

			if (rep_explosions[i].big)
			{
				JE_setupExplosionLarge(false, 2, tempX, tempY);

				if (rep_explosions[i].ttl == 1 || mt_rand() % 5 == 1)
					soundQueue[7] = S_EXPLOSION_11;
				else
					soundQueue[6] = S_EXPLOSION_9;

				rep_explosions[i].delay = 4 + (mt_rand() % 3);
			}
			else
			{
				JE_setupExplosion(tempX, tempY, 0, 1, false, false);

				soundQueue[5] = S_EXPLOSION_4;

				rep_explosions[i].delay = 3;
			}

			explosionFilter = 0;
			rep_explosions[i].ttl--;
		}
	}

	/* Draw explosions. */
	for (int j = 0; j < MAX_EXPLOSIONS; j++)
	{
		if (explosions[j].ttl != 0)
		{
			if (!explosions[j].fixedPosition)
			{
				explosions[j].sprite++;
				explosions[j].y += explodeMove;
			}
			else if (explosions[j].followPlayer)
			{
				explosions[j].x += explosionFollowAmountX;
				explosions[j].y += explosionFollowAmountY;
			}
			explosions[j].y += explosions[j].deltaY;

			if (explosions[j].y > vga_height - 14)
			{
				explosions[j].ttl = 0;
			}
			else
			{
				// Skip far-offscreen explosion drawing because these blitters do not clip X.
				// Lifetime still advances, and the bounds retain every visible pixel.
				if (explosions[j].x > -12 && explosions[j].x < 344)
				{
					// Stable per-instance id: puff churn recycles slots, and a plain slot id
					// would mis-pair a recycled slot with its previous occupant. Fold in the
					// per-slot generation (4 values disambiguate consecutive reuses); j*4 + 3
					// stays within the EXPL id range (MAX_EXPLOSIONS*4 < 1000).
					rl_current_id = RL_ID_EXPL_BASE + j * 4 + (explosions[j].id_gen & 3);
					const int ex = explosions[j].x, ey = explosions[j].y;
					const unsigned int frame = explosions[j].sprite + 1;
					const Uint8 tint = explosions[j].filter;  // endless elite / champion
					const Uint8 alpha = explosion_opacity(j);
					// Shield bubbles never combine partial opacity with an Endless tier tint.
					if (alpha < NET_STYLE_SOLID)
						blit_sprite2_alpha(VGAScreen, ex, ey, explosionSpriteSheet, frame, -1, alpha);
					else if (explosionTransparent && tint != 0)
						blit_sprite2_blend_filter(VGAScreen, ex, ey, explosionSpriteSheet, frame,
						                          tint | ENDLESS_EXPLOSION_BRIGHT);
					else if (explosionTransparent)
						blit_sprite2_blend(VGAScreen, ex, ey, explosionSpriteSheet, frame);
					else if (tint != 0)
						blit_sprite2_filter(VGAScreen, ex, ey, explosionSpriteSheet, frame, tint);
					else
						blit_sprite2(VGAScreen, ex, ey, explosionSpriteSheet, frame);
					rl_current_id = 0;
				}

				explosions[j].ttl--;
			}
		}
	}

	if (!portConfigChange)
		portConfigDone = true;

	/* Background layer 2. */
	if (!(smoothies[2-1] && processorType < 4) &&
	    !(smoothies[1-1] && processorType == 3))
	{
		if (background2over == 2)
		{
			if (wild && !background2notTransparent)
				draw_background_2_blend(VGAScreen);
			else
				draw_background_2(VGAScreen);
		}
	}

	// Draw sparks before residual capture so replay interpolation owns them. Silent re-simulation
	// skips this presentation-only state.
	if (!rollback_resim_silent)
		JE_drawSP();

	// Record enemy bars before smoothie residual capture; normal levels draw them after filtering.
	if (anySmoothies)
	{
		draw_enemy_health_bars();
		hud_draw_ship_hp_bars();
	}

	// Capture smoothie residuals between recorded blits and non-blit overlays.
	if (anySmoothies && !rollback_resim_silent)
		memcpy(VGAScreen2->pixels, game_screen->pixels, (size_t)game_screen->h * game_screen->pitch);

	/* Warning indicator. */
	if ((player[0].is_alive && player[0].armor < 6) ||
	    (twoPlayerMode && !galagaMode && player[1].is_alive && player[1].armor < 6))
	{
		int armor_amount = (player[0].is_alive && player[0].armor < 6) ? player[0].armor : player[1].armor;

		if (armorShipDelay > 0)
		{
			armorShipDelay--;
		}
		else
		{
			tempW = 560;
			b = JE_newEnemy(50, tempW, 0);
			if (b > 0)
			{
				enemy[b-1].enemydie = 560 + (mt_rand() % 3) + 1;
				enemy[b-1].eyc -= backMove3;
				enemy[b-1].armorleft = 4;
				enemy_note_full_armor(&enemy[b-1]);
			}
			armorShipDelay = 500;
		}

		if ((player[0].is_alive && player[0].armor < 6 && (!isNetworkGame || thisPlayerNum == 1)) ||
		    (twoPlayerMode && player[1].is_alive && player[1].armor < 6 && (!isNetworkGame || thisPlayerNum == 2)))
		{

			tempW = armor_amount * 4 + 8;
			if (warningSoundDelay > tempW)
				warningSoundDelay = tempW;

			if (warningSoundDelay > 1)
			{
				warningSoundDelay--;
			}
			else
			{
				if (armorAlarm)
					soundQueue[7] = S_WARNING;
				warningSoundDelay = tempW;
			}

			warningCol += warningColChange;
			if (warningCol > 113 + (14 - (armor_amount * 2)))
			{
				warningColChange = -warningColChange;
				warningCol = 113 + (14 - (armor_amount * 2));
			}
			else if (warningCol < 113)
			{
				warningColChange = -warningColChange;
			}
			const int playfield_left = PLAYFIELD_LEFT;
			const int playfield_right = playfield_left + PLAYFIELD_WIDTH - 1;
			const char warning_text[] = "WARNING";
			const int warning_text_width = JE_textWidth(warning_text, TINY_FONT);
			const int warning_x = playfield_left + (PLAYFIELD_WIDTH - warning_text_width) / 2;
			const int gap_margin = 1;
			warning_bar(playfield_left, 181, warning_x - gap_margin - 1, 183, warningCol);
			warning_bar(warning_x + warning_text_width + gap_margin, 181, playfield_right, 183, warningCol);
			warning_bar(playfield_left, 0, playfield_right, 3, warningCol);

			JE_outText(VGAScreen, warning_x, 178, warning_text, 7, (warningCol % 16) / 2);

		}
	}

	/* Random explosions. */
	// Full visible playfield: 280 is the pre-widescreen width and stops short of the widened right
	// edge (see composite_playfield / video.h); 184 = full playfield height (vanilla stopped at 180).
	if (randomExplosions && mt_rand() % 10 == 1)
	{
		// Sequenced: as arguments the two draws were unordered, and the compilers disagree
		// about which coordinate gets which (see varz.c's special scatter).
		const int boom_x = PLAYFIELD_LEFT + mt_rand() % PLAYFIELD_WIDTH;
		const int boom_y = mt_rand() % 184;
		JE_setupExplosionLarge(false, 20, boom_x, boom_y);
	}

	// Silent re-simulation and loadout edits both leave the sidekick HUD dirty.
	if (hud_sidekicks_dirty && !rollback_resim_silent)
	{
		hud_sidekicks_dirty = false;
		JE_drawOptionsHUD();
		JE_drawPortConfigButtons();
	}

	// Same for the shield/armor gauges: their painters go quiet during silent passes
	// (varz.c) and every snapshot restore raises the flag, so one repaint here settles
	// the HUD on the corrected state instead of whatever a resim pass painted last.
	if (hud_bars_dirty && !rollback_resim_silent)
	{
		hud_bars_dirty = false;
		JE_repaintShieldArmorBars();
	}

	// And the message bar, whose line is posted from inside the simulation: the pickup that
	// announces it often lands on a corrected frame, and that pass draws nothing. The erase
	// countdown bounds it, so a line that already expired is not resurrected.
	if (hud_message_dirty && !rollback_resim_silent)
	{
		hud_message_dirty = false;
		if (textErase > 0)
			JE_repaintTextWindow();
	}

	// Play link cues from presented state so rollback neither loses nor repeats them.
	if (!rollback_resim_silent && twoPlayerLinked != link_cue_state)
	{
		link_cue_state = twoPlayerLinked;
		if (linkSounds && !galagaMode)
		{
			const JE_byte cue = twoPlayerLinked ? S_CLINK : S_SPRING;
			multiSamplePlay(soundSamples[cue-1], soundSampleCount[cue-1], SFX_CUE_CHANNEL, fxPlayVol / 2);
		}
	}

	/* Sound queue. */
	if (firstGameOver)
	{
		temp = 0;
		for (temp2 = 0; temp2 < COUNTOF(soundQueue); temp2++)
		{
			if (soundQueue[temp2] != S_NONE)
			{
				temp = soundQueue[temp2];
				if (temp2 == 3)
					temp3 = fxPlayVol;
				else if (temp == 15)
					temp3 = fxPlayVol / 4;
				else   /*Lightning*/
					temp3 = fxPlayVol / 2;

				// A rollback re-simulation drains the queue silently: these ticks'
				// sounds already played when the tick first ran.  The temp/temp3
				// writes above still happen; they are registered scratch state
				// and a replayed tick must mutate them identically.
				if (!rollback_resim)
					multiSamplePlay(soundSamples[temp-1], soundSampleCount[temp-1], temp2, temp3);

				soundQueue[temp2] = S_NONE;
			}
		}
	}

	if (returnActive && enemyOnScreen == 0)
	{
		JE_eventJump(65535);
		returnActive = false;
	}

	/* Debug counters. */
	debugTime = SDL_GetTicks();
	tempW = lastmouse_but;

	if (debug)
	{
		for (size_t i = 0; i < 9; i++)
		{
			tempStr[i] = '0' + smoothies[i];
		}
		tempStr[9] = '\0';
		sprintf(buffer, "SM = %s", tempStr);
		JE_outText(VGAScreen, 30, 70, buffer, 4, 0);

		sprintf(buffer, "Memory left = %d", -1);
		JE_outText(VGAScreen, 30, 80, buffer, 4, 0);
		sprintf(buffer, "Enemies onscreen = %d", enemyOnScreen);
		JE_outText(VGAScreen, 30, 90, buffer, 6, 0);

		debugHist = debugHist + abs((JE_longint)debugTime - (JE_longint)lastDebugTime);
		debugHistCount++;
		sprintf(tempStr, "%2.3f", 1000.0f / roundf(debugHist / debugHistCount));
		sprintf(buffer, "X:%d Y:%-5d  %s FPS  %d %d %d %d", (mapX - 1) * 12 + player[0].x, curLoc, tempStr, player[0].x_velocity, player[0].y_velocity, player[0].x, player[0].y);
		JE_outText(VGAScreen, 45, 175, buffer, 15, 3);
		lastDebugTime = debugTime;
	}

	/*Pentium Speed Mode?*/
	if (pentiumMode)
	{
		frameCountMax = (frameCountMax == 2) ? 3 : 2;
	}

	/* Level timer. */
	if (levelTimer && levelTimerCountdown > 0)
	{
		levelTimerCountdown--;
		if (levelTimerCountdown == 0)
			JE_eventJump(levelTimerJumpTo);

		if (timedBattleMode)
		{
			// No-op; play no sound effects
		}
		else if (levelTimerCountdown > 200)
		{
			if (levelTimerCountdown % 100 == 0)
				soundQueue[7] = S_WARNING;

			if (levelTimerCountdown % 10 == 0)
				soundQueue[6] = S_CLICK;
		}
		else if (levelTimerCountdown % 20 == 0)
		{
			soundQueue[7] = S_WARNING;
		}

		// Don't use floats due to rounding.
		sprintf(buffer, "%d.%d", levelTimerCountdown / 100, (levelTimerCountdown / 10) % 10);

		// Lay the countdown number and its "TIME" label out as one group and centre
		// that group on the playfield.
		const int label_w = JE_textWidth(miscText[66], TINY_FONT);
		const int counter_w = JE_textWidth(buffer, SMALL_FONT_SHAPES);
		const int group_w = counter_w + 3 + label_w;
		const int counter_x = PLAYFIELD_CENTER_X(group_w);
		const int label_x = counter_x + counter_w + 3;

		JE_textShade(VGAScreen, label_x, 6, miscText[66], 7, (levelTimerCountdown % 20) / 3, FULL_SHADE);
		JE_dString(VGAScreen, counter_x, 2, buffer, SMALL_FONT_SHAPES);
	}

	/* Game over. */
	if (!constantPlay && !constantDie)
	{
		if (allPlayersGone)
		{
			if (player[0].exploding_ticks > 0 || player[1].exploding_ticks > 0)
			{
				if (galagaMode)
					player[1].exploding_ticks = 0;

				musicFade = true;

				// Fresh non-Esc input skips the Relaxed wreck animation. Held steering must be released first.
				if (endlessDeathMenuDue() && !play_demo && !rollback_resim)
				{
					push_joysticks_as_keyboard();
					service_SDL_events(false);

					// Steering touches use mouse_pressed[0], not mousedown or newmouse.
					if (!keydown && !mousedown && !joydown && !mouse_pressed[0])
					{
						deathSkipArmed = true;
					}
					else if (deathSkipArmed && ((newkey && lastkey_scan != SDL_SCANCODE_ESCAPE) ||
					                            newmouse || mouse_pressed[0]))
					{
						reallyEndLevel = true;
						// A live-input one-shot outside the movement tuples; record it so a
						// self-test replay of this tick lands on the same state.
						if (rollback_selftest_active())
							rollback_st_event(RB_EV_DISMISS);
					}
				}
			}
			else
			{
				// Relaxed mode plays the normal GAME OVER beat before opening its death menu.
				if (play_demo || normalBonusLevelCurrent || bonusLevelCurrent)
					reallyEndLevel = true;
				else
				{
					const int playfield_left = PLAYFIELD_LEFT;
					const int game_over_width = JE_textWidth(miscText[21], FONT_SHAPES);
					const int game_over_x = playfield_left + (PLAYFIELD_WIDTH - game_over_width) / 2;
					JE_dString(VGAScreen, game_over_x, 60, miscText[21], FONT_SHAPES); // game over
				}

				if (firstGameOver)
				{
					if (!play_demo)
					{
						play_song(SONG_GAMEOVER);
						// Cancel the death fade before restoring volume for the game-over song.
						musicFade = false;
						set_volume(tyrMusicVolume, fxVolume);
					}
					// Require a fresh press for GAME OVER, including the held touch-fire latch.
					newkey = newmouse = false;
					mouse_pressed[0] = false;
					firstGameOver = false;
				}

				// Open the Relaxed death menu when the game-over song ends, bounded by min/max timers.
				if (endlessDeathMenuDue() && !play_demo && !rollback_resim)
				{
					++deathGameOverTicks;

					if ((!playing && deathGameOverTicks >= DEATH_GAMEOVER_TICKS_MIN) ||
					    deathGameOverTicks >= DEATH_GAMEOVER_TICKS_MAX)
					{
						reallyEndLevel = true;
						// Record the automatic dismissal for self-test replay.
						if (rollback_selftest_active())
							rollback_st_event(RB_EV_DISMISS);
					}
				}

				if (!play_demo && !rollback_resim)
				{
					push_joysticks_as_keyboard();
					// Poll without clearing: the smooth-present loop in JE_starShowVGA
					// already consumed inter-tick SDL events into newkey/newmouse, so
					// clearing here would discard the press and GAME OVER would never respond.
					service_SDL_events(false);
					// Dead-player touch input still arrives through mouse_pressed[0].
					if ((newkey || button[0] || button[1] || button[2]) || newmouse || mouse_pressed[0])
					{
						reallyEndLevel = true;
						// A live-input one-shot outside the movement tuples; record it
						// so a self-test replay of this tick lands on the same state.
						if (rollback_selftest_active())
							rollback_st_event(RB_EV_DISMISS);
					}
				}

				if (isNetworkGame)
					reallyEndLevel = true;
			}
		}
	}

	if (rollback_resim)
	{
		// Replay pass: live input is never sampled.  The only sim effects the
		// blocks below can have came from one-shot input events, re-applied
		// here from the recorded bits (self-test; netplay resim derives its
		// pause/menu/cheat handling from the input tuples instead).
		if (rollback_st_events() & RB_EV_DISMISS)
			reallyEndLevel = true;
	}
	else if (play_demo) // input kills demo
	{
		push_joysticks_as_keyboard();
		service_SDL_events(false);

		if (newkey || newmouse)
		{
			reallyEndLevel = true;
			if (rollback_selftest_active())
				rollback_st_event(RB_EV_DISMISS);

			stopped_demo = true;
		}
	}
	else // input handling for pausing, menu, cheats
	{
		service_SDL_events(false);

		if (newkey)
		{
			skipStarShowVGA = false;
			JE_mainKeyboardInput();
			newkey = false;
			if (skipStarShowVGA)
			{
				// Mid-tick restart (in-game help / cheat overlays): the tick is
				// not cleanly replayable, so the self-test skips verifying it.
				rollback_taint("skipStarShowVGA");
				goto level_loop;
			}
		}

		/* Online has no pause: it stops one machine's clock and not the other's, and losing
		 * window focus is not a reason to halt a session the other player is still flying.
		 * Swallow the press so a held key or a queued joystick button cannot bank it. */
		if (pause_pressed || !windowHasFocus)
		{
			pause_pressed = false;

			if (!isNetworkGame)
				JE_pauseGame();
		}

		// Endless Standard and Hardcore: the pause menu is off-limits from the moment the ship dies.
		if (ingamemenu_pressed && endlessDeathLocksMenu() && all_players_dead())
			ingamemenu_pressed = false;

		if (ingamemenu_pressed)
		{
			ingamemenu_pressed = false;

			if (isNetworkGame)
			{
				inGameMenuRequest = true;
			}
			else
			{
				yourInGameMenuRequest = true;
				JE_doInGameSetup();
				skipStarShowVGA = true;
			}
		}
	}

	// Snapshot ship velocity inside the simulation so replayed inherited-velocity shots agree.
	ship_pred_on_tick();

	/* Restart backgrounds or end the level when no enemies remain. This is simulation state and
	 * must run before the rollback driver. */
	if (stopBackgroundNum == 9 && backMove == 0 && !enemyStillExploding)
	{
		backMove = 1;
		backMove2 = 2;
		backMove3 = 3;
		explodeMove = 2;
		stopBackgroundNum = 0;
		stopBackgrounds = false;
		if (waitToEndLevel)
		{
			endLevel = true;
			levelEnd = 40;
		}
		if (allPlayersGone)
		{
			reallyEndLevel = true;
		}
	}

	if (!endLevel && enemyOnScreen == 0)
	{
		if (readyToEndLevel && !enemyStillExploding)
		{
			if (levelTimerCountdown > 0)
			{
				levelTimer = false;
			}
			readyToEndLevel = false;
			endLevel = true;
			levelEnd = 40;
			if (allPlayersGone)
			{
				reallyEndLevel = true;
			}
		}
		if (stopBackgrounds)
		{
			stopBackgrounds = false;
			backMove = 1;
			backMove2 = 2;
			backMove3 = 3;
			explodeMove = 2;
		}
	}

	// Cull unreachable above-screen orphans after a prolonged map stop. Live linked boss anchors
	// remain untouched.
	enemyParkedAbove = count_stuck_above_screen();
	if (!endLevel && stopBackgrounds && !forceEvents && enemyParkedAbove != 0)
	{
		if (++mapStopStallTicks >= MAP_STOP_STALL_LIMIT)
		{
			for (int i = 0; i < 100; i++)
				if (enemyAvail[i] != 1 && enemy_stuck_orphaned(i))
					enemyAvail[i] = 1;
			mapStopStallTicks = 0;
		}
	}
	else
		mapStopStallTicks = 0;

	// Level fade-in / flash ramp.  Sim state, so it advances here with the rest of the
	// tick rather than down in the filtration draw below: everything past the netcode
	// driver is skipped by re-simulated frames (JE_advanceLevelFade).
	JE_advanceLevelFade();

#ifdef WITH_NETWORK
	/* Wire-test scripted events, keyed to the frame counter so re-simulation passes replay them
	 * exactly. */
	if (qa_net_gameplay_ticks > 0 && isNetworkGame && nrb_active())
	{
		if (qa_net_lobby_settings && nrb_frame() >= 200 && nrb_frame() <= 320
		    && nrb_frame() % 40 == 0)
			player_award_pickup_cash(&player[(nrb_frame() / 40) & 1], 75);

		/* Exercise each Super Arcade color through the real pickup rule for both ships.
		 * Resolve against the collector's arsenal to avoid divergent loadouts. */
		if (network_game_type == NETWORK_GAME_SUPERARCADE && nrb_frame() >= 200
		    && nrb_frame() <= 400 && nrb_frame() % 50 == 0)
		{
			const uint slot = (uint)(nrb_frame() / 50) - 4;   // 0..4
			const uint w1 = player_sa_ball_weapon(&player[0], slot);
			const uint w2 = player_sa_ball_weapon(&player[1], slot);
			player[0].items.weapon[FRONT_WEAPON].id = (Uint8)w1;
			player[1].items.weapon[FRONT_WEAPON].id = (Uint8)w2;
			player[0].shot_multi_pos[SHOT_FRONT] = player[1].shot_multi_pos[SHOT_FRONT] = 0;
			if (!rollback_resim)
			{
				printf("NET SA BALL slot=%u p1=%u p2=%u\n", slot, w1, w2);
				fflush(stdout);
			}
		}

		if (qa_net_zones > 0 && nrb_frame() == QA_NET_ZONE_END_FRAME && !endLevel)
		{
			endLevel = true;
			levelEnd = 40;
		}
	}
#endif

	/*Network Update*/
#ifdef WITH_NETWORK
	if (isNetworkGame && nrb_active())
	{
		// Rollback driver: publish this frame's input, ingest the peer's, and
		// either fall through to present or restore-and-re-simulate.  The
		// re-simulation passes re-enter the loop with rollback_resim set; the
		// last one records rendering and falls through here to be presented.
		if (nrb_driver() == NRB_STEP_RESIM)
			goto level_loop;
	}
	else if (isNetworkGame)
	{
		if (!reallyEndLevel)
		{
			// Bit 0 was the pause request. Online cannot pause, so it stays clear.
			Uint16 requests = (inGameMenuRequest == true) << 1 |
			                  (skipLevelRequest == true) << 2 |
			                  (nortShipRequest == true) << 3;
			SDLNet_Write16(requests,        &packet_state_out[0]->data[14]);

			SDLNet_Write16(difficultyLevel, &packet_state_out[0]->data[16]);
			SDLNet_Write16(player[0].x,     &packet_state_out[0]->data[18]);
			SDLNet_Write16(player[1].x,     &packet_state_out[0]->data[20]);
			SDLNet_Write16(player[0].y,     &packet_state_out[0]->data[22]);
			SDLNet_Write16(player[1].y,     &packet_state_out[0]->data[24]);
			SDLNet_Write16(curLoc,          &packet_state_out[0]->data[26]);

			// Desync canary: a summary of the simulation as it stands at this point in the
			// tick, which both machines reach with identical state when all is well.
			{
				Uint32 rand_draws, player_hash, enemy_hash;
				network_sim_state(&rand_draws, &player_hash, &enemy_hash);
				SDLNet_Write32(rand_draws,  &packet_state_out[0]->data[NET_STATE_RAND]);
				SDLNet_Write32(player_hash, &packet_state_out[0]->data[NET_STATE_PHASH]);
				SDLNet_Write32(enemy_hash,  &packet_state_out[0]->data[NET_STATE_EHASH]);
			}

			network_state_send();

			if (network_state_update())
			{
				assert(SDLNet_Read16(&packet_state_in[0]->data[26]) == SDLNet_Read16(&packet_state_out[network_delay]->data[26]));

				// Compare packets from the same logical tick and report only the first mismatch per level.
				{
					static int reported_for_level = -1;

					const Uint32 their_rand = SDLNet_Read32(&packet_state_in[0]->data[NET_STATE_RAND]);
					const Uint32 our_rand   = SDLNet_Read32(&packet_state_out[network_delay]->data[NET_STATE_RAND]);
					const Uint32 their_ph   = SDLNet_Read32(&packet_state_in[0]->data[NET_STATE_PHASH]);
					const Uint32 our_ph     = SDLNet_Read32(&packet_state_out[network_delay]->data[NET_STATE_PHASH]);
					const Uint32 their_eh   = SDLNet_Read32(&packet_state_in[0]->data[NET_STATE_EHASH]);
					const Uint32 our_eh     = SDLNet_Read32(&packet_state_out[network_delay]->data[NET_STATE_EHASH]);

					if ((their_rand != our_rand || their_ph != our_ph || their_eh != our_eh) &&
					    reported_for_level != (int)mainLevel)
					{
						reported_for_level = (int)mainLevel;

						if (qa_net_scenario == 19)
						{
							Uint32 link_bits = 0;
							memcpy(&link_bits, &linkGunDirec,
							       MIN(sizeof(link_bits), sizeof(linkGunDirec)));
							fprintf(stderr,
							        "NET DELAY DESYNC player=%u scroll=%u ph=%08x/%08x "
							        "pos=%d,%d/%d,%d shield=%d/%d linked=%d dir=%08x input=%u,%04x/%u,%04x\n",
							        thisPlayerNum, (unsigned)curLoc, (unsigned)our_ph,
							        (unsigned)their_ph, player[0].x, player[0].y,
							        player[1].x, player[1].y, player[0].shield, player[1].shield,
							        twoPlayerLinked ? 1 : 0,
							        (unsigned)link_bits,
							        (unsigned)SDLNet_Read16(
							                &packet_state_out[network_delay]->data[NET_STATE_LINK_FLAGS]),
							        (unsigned)SDLNet_Read16(
							                &packet_state_out[network_delay]->data[NET_STATE_LINK_ANGLE]),
							        (unsigned)SDLNet_Read16(
							                &packet_state_in[0]->data[NET_STATE_LINK_FLAGS]),
							        (unsigned)SDLNet_Read16(
							                &packet_state_in[0]->data[NET_STATE_LINK_ANGLE]));
							fflush(stderr);
						}

						// Goes through the net log, not stderr: this is a Windows-subsystem
						// build with no console, so a printf would be thrown away.
						char detail[512];
						snprintf(detail, sizeof(detail),
						         "level %d (scroll clock %u), player %u, delay %d\n"
						         "  rand draws : local %lu  remote %lu  %s\n"
						         "  players    : local %08x  remote %08x  %s\n"
						         "  enemies    : local %08x  remote %08x  %s",
						         (int)mainLevel, (unsigned)curLoc, thisPlayerNum, network_delay,
						         (unsigned long)our_rand, (unsigned long)their_rand,
						         our_rand == their_rand ? "ok" : "DIFFERS",
						         (unsigned)our_ph, (unsigned)their_ph,
						         our_ph == their_ph ? "ok" : "DIFFERS",
						         (unsigned)our_eh, (unsigned)their_eh,
						         our_eh == their_eh ? "ok" : "DIFFERS");
						network_diag_note_desync((int)mainLevel);
						crashlog_note_net("NETWORK DESYNC", detail);

						if (networkDesyncHalt)
							network_tyrian_halt(7, false);
					}
				}

				requests = SDLNet_Read16(&packet_state_in[0]->data[14]) ^ SDLNet_Read16(&packet_state_out[network_delay]->data[14]);
				// Bit 0 was the pause request. Nothing online sets it now; the bit stays
				// reserved so the state packet keeps its layout.
				if (requests & 2)
				{
					yourInGameMenuRequest = SDLNet_Read16(&packet_state_out[network_delay]->data[14]) & 2;
					JE_doInGameSetup();
					yourInGameMenuRequest = false;
					if (haltGame)
						reallyEndLevel = true;
				}
				if (requests & 4)
				{
					levelTimer = true;
					levelTimerCountdown = 0;
					endLevel = true;
					levelEnd = 40;
				}
				if (requests & 8) // nortship
				{
					player[0].items.ship = 12;                     // Nort Ship
					player[0].items.special = 13;                  // Astral Zone
					player[0].items.weapon[FRONT_WEAPON].id = 36;  // NortShip Super Pulse
					player[0].items.weapon[REAR_WEAPON].id = 37;   // NortShip Spreader
					shipGr = 1;
				}

				for (int i = 0; i < 2; i++)
				{
					if (SDLNet_Read16(&packet_state_in[0]->data[18 + i * 2]) != SDLNet_Read16(&packet_state_out[network_delay]->data[18 + i * 2]) || SDLNet_Read16(&packet_state_in[0]->data[20 + i * 2]) != SDLNet_Read16(&packet_state_out[network_delay]->data[20 + i * 2]))
					{
						char temp[64];
						sprintf(temp, "Player %d is unsynchronized!", i + 1);

						JE_textShade(game_screen, 40, 110 + i * 10, temp, 9, 2, FULL_SHADE);
					}
				}

				if (qa_net_scenario == 19 && ++qa_net_delay_frames > qa_net_gameplay_ticks)
				{
					const int rc = network_desync_count() == 0 && qa_net_special_flashes > 0 ? 0 : 1;
					printf("NET DELAY %s player=%u frames=%lu desyncs=%lu special-flashes=%lu\n",
					       rc == 0 ? "PASS" : "FAIL", thisPlayerNum, qa_net_delay_frames,
					       (unsigned long)network_desync_count(), qa_net_special_flashes);
					fflush(stdout);

					const Uint32 drain_started = SDL_GetTicks();
					while (SDL_GetTicks() - drain_started < 1200)
					{
						watchdog_heartbeat();
						network_check();
						SDL_Delay(1);
					}
					exit(rc);
				}
			}
		}

		JE_clearSpecialRequests();
	}
#endif

	// Replay the tick from recorded input and compare every registered byte (rollback.h).
	if (rollback_selftest_tick())
	{
		rl_abort_record();
		JE_discardSPPass();  // separate from rl_abort_record, which returns early when not recording
		goto level_loop;
	}

	/*Filtration*/
	// Special Tint off drops the flare's wash here, at the paint, rather than at the flare that
	// installed it: levelFilter is simulation state, so a peer playing with the other setting
	// would desync against a flare that never set it.
	if (filterActive && !(flareOwnsFilter && !specialScreenTint))
	{
		if (render_list_recording)
			rl_rec_filter_screen(levelFilter, levelBrightness);

		// Smoothie residual: any full-screen grade; colour flare (levelFilter != -99) or
		// brightness-only flash (levelBrightness != -99); must hit the pre-overlay snapshot too,
		// or it bakes into the residual and freezes the playfield.
		if (anySmoothies)
			JE_filterScreenApply(VGAScreen2, levelFilter, levelBrightness);

		JE_filterScreenApply(VGAScreen, levelFilter, levelBrightness);
	}

	// Smoothie levels already drew+recorded the enemy bars before the residual
	// snapshot (above) so they interpolate; here we draw them for normal levels only.
	if (!anySmoothies)
	{
		draw_enemy_health_bars();
		hud_draw_ship_hp_bars();
	}
	draw_boss_bar();
	JE_updateGaugeFlash();

	JE_inGameDisplays();

	// Render-list capture for this tick ends here (everything below composites
	// or presents; it does not draw into the playfield).
	rl_end_record();
	rl_finalize();  // match against previous frame -> per-command motion deltas
	if (!anySmoothies)
		rl_capture_residual(game_screen, VGAScreen2);  // non-blit pixels (superpixels, boss bar)
	else
		// Reapply overlay-only changes to the display frame without filtering.
		rl_capture_residual_delta(VGAScreen2, game_screen);
	vt_ship_tick();       // fold external forces / repositions into the variable-dt ship
	// Publish tick velocity for render interpolation of ship-attached shots.
	for (int p = 0; p < (twoPlayerMode ? 2 : 1); ++p)
		rl_set_ship_vel(p, ship_vel_x[p], ship_vel_y[p]);
#if RL_SELFTEST
	{
		static int seen = 0;
		if (seen < 5)
		{
			++seen;
			fprintf(stderr, "RL: in-game frame %d, anySmoothies=%d filterActive=%d cmds=%zu\n",
			        seen, (int)anySmoothies, (int)filterActive, rl_count());
		}
	}
	// Without smoothie per-pixel effects the replayed list (including the recorded
	// full-screen colour filter) must reproduce game_screen exactly.
	if (!anySmoothies)
	{
		const size_t mism = rl_replay_and_compare(VGAScreen2, game_screen);
		static int clean_frames = 0, bad_frames = 0;
		++clean_frames;
		if (mism != 0)
		{
			++bad_frames;
			if (bad_frames <= 20)
				fprintf(stderr, "RL selftest: %zu mismatched bytes (clean frame %d, %zu cmds)\n",
				        mism, clean_frames, rl_count());
		}
		else if (clean_frames % 120 == 1)
		{
			fprintf(stderr, "RL selftest: clean frame %d OK (%zu cmds)\n", clean_frames, rl_count());
		}
	}
#endif

	VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

	JE_starShowVGA();

	if (reallyEndLevel)
	{
		goto start_level;
	}
	goto level_loop;
}

// Full-screen picture-wipe styles used by the level-script commands U / V / R.
typedef enum
{
	WIPE_U,  // vertical wipe (new picture slides in over the old)
	WIPE_V,  // vertical wipe (new picture revealed from the opposite edge)
	WIPE_R,  // horizontal wipe
} WipeKind;

// Composite one frame of a picture wipe at boundary position z: the old image
// (VGAScreen2) and the new image (pic_buffer) meet at row/column z. These are the
// original per-step inner loops verbatim; only the outer pacing changes, in
// animate_picture_wipe below.
static void compose_wipe_frame(WipeKind kind, int z, const Uint8 *pic_buffer)
{
	const int pitch = VGAScreen->pitch;
	Uint8 *vga = VGAScreen->pixels;
	Uint8 *vga2 = VGAScreen2->pixels;
	const Uint8 *pic;

	switch (kind)
	{
	case WIPE_U:
		pic = pic_buffer + (vga_height - 1 - z) * pitch;
		for (int y = 0; y < vga_height; y++)
		{
			if (y <= z)
			{
				memcpy(vga, pic, pitch);
				pic += pitch;
			}
			else
			{
				memcpy(vga, vga2, pitch);
				vga2 += pitch;
			}
			vga += pitch;
		}
		break;

	case WIPE_V:
		pic = pic_buffer;
		for (int y = 0; y < vga_height; y++)
		{
			if (y <= vga_height - 1 - z)
			{
				memcpy(vga, vga2, pitch);
				vga2 += pitch;
			}
			else
			{
				memcpy(vga, pic, pitch);
				pic += pitch;
			}
			vga += pitch;
		}
		break;

	case WIPE_R:
		pic = pic_buffer;
		for (int y = 0; y < vga_height; y++)
		{
			memcpy(vga, vga2 + z, pitch - 1 - z);
			vga += pitch - z;
			vga2 += VGAScreen2->pitch;
			memcpy(vga, pic, z + 1);
			vga += z;
			pic += pitch;
		}
		break;
	}
}

// Animate a full-screen wipe at tick rate or display rate, with the same duration
// and final image. Input skips to the end.
static void animate_picture_wipe(WipeKind kind, const Uint8 *pic_buffer)
{
	const int steps = (kind == WIPE_R) ? (VGAScreen->pitch - 1) : vga_height;

	if (smoothMotion)
	{
		const float duration = steps * get_delay_period();  // ms; == classic (steps x one delay unit)
		const Uint64 freq = SDL_GetPerformanceFrequency();
		const Uint64 begin = SDL_GetPerformanceCounter();
		const float counter_to_ms = 1000.0f / (float)freq;

		for (;;)
		{
			if (newkey)
				break;

			const float t = (float)(SDL_GetPerformanceCounter() - begin) * counter_to_ms / duration;
			const bool done = t >= 1.0f;
			int z = done ? steps - 1 : (int)(t * steps);
			if (z > steps - 1)
				z = steps - 1;

			compose_wipe_frame(kind, z, pic_buffer);
			JE_showVGA();

			if (done)
				break;

			if (!output_vsync)
				limit_render_fps();
			service_SDL_events(false);
		}
	}
	else
	{
		for (int z = 0; z < steps; z++)
		{
			if (!newkey)
			{
				setDelay(1);
				compose_wipe_frame(kind, z, pic_buffer);
				JE_showVGA();
				service_wait_delay();
			}
		}
	}
}

// Backdate E5 SAVARA cannon groups so their low sprites scroll in together.
static void event_backdate_savara_cannons(void)
{
	if (episodeNum != 5 || lvlFileNum != 4)
		return;

	for (JE_word i = 0; i < maxEvent; ++i)
	{
		if (eventRec[i].eventtype != 6 || eventRec[i].eventdat5 <= 0)
			continue;

		const JE_word time = eventRec[i].eventtime;
		const JE_byte link = eventRec[i].eventdat4;

		int shift = 0;
		for (JE_word j = 0; j < maxEvent; ++j)
		{
			if (eventRec[j].eventtype == 6 && eventRec[j].eventtime == time &&
			    eventRec[j].eventdat4 == link && eventRec[j].eventdat5 > shift)
				shift = eventRec[j].eventdat5;
		}
		if (time <= (JE_word)shift)
			continue;

		for (JE_word j = 0; j < maxEvent; ++j)
		{
			if (eventRec[j].eventtype == 6 && eventRec[j].eventtime == time &&
			    eventRec[j].eventdat4 == link)
			{
				eventRec[j].eventtime -= shift;
				eventRec[j].eventdat5 -= shift;
			}
		}
	}

	for (JE_word i = 1; i < maxEvent; ++i)
	{
		const struct JE_EventRecType moved = eventRec[i];
		JE_word j = i;
		while (j > 0 && eventRec[j - 1].eventtime > moved.eventtime)
		{
			eventRec[j] = eventRec[j - 1];
			--j;
		}
		eventRec[j] = moved;
	}
}

/* --- Load Level/Map Data --- */
void JE_loadMap(void)
{
	JE_DanCShape shape;

	JE_word x, y;
	JE_integer yy;
	JE_word mapSh[3][128]; /* [1..3, 0..127] */
	JE_byte *ref[3][128]; /* [1..3, 0..127] */
	char s[256];

	static JE_byte mapBuf[15 * 600]; /* [1..15 * 600] */
	JE_word bufLoc;

	char buffer[256];
	int i;
	static Uint8 pic_buffer[vga_width * vga_height]; /* reusable 8-bit wipe buffer */

	crashlog_set_phase("loading level map");

	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: JE_loadMap mainLevel=%d gameLoaded=%d\n",
		        (int)mainLevel, (int)gameLoaded);
		fflush(stderr);
	}

	lastCubeMax = cubeMax;

	/*Defaults*/
	songBuy = DEFAULT_SONG_BUY;  /*Item Screen default song*/

	/* Load LEVELS.DAT - Section = MAINLEVEL */
	saveLevel = mainLevel;

new_game:
	set_menu_centered(true);
	galagaMode = false;
	useLastBank = false;
	extraGame = false;
	engageMode = false;
	haltGame = false;

	gameLoaded = false;

	if (!play_demo)
	{
		do
		{
			FILE *ep_f = dir_fopen_die(JE_episodeDir(), episode_file, "rb");
			const long ep_end = ftell_eof(ep_f);  // guard the scans below against reading past EOF

			jumpSection = false;
			loadLevelOk = false;

			/* Bound the mainLevel section scan by EOF and recover invalid saves to the title screen. */
			int x = 0;
			while (x < mainLevel)
			{
				if (ftell(ep_f) >= ep_end)
				{
					char detail[192];
					snprintf(detail, sizeof(detail),
					         "episode %d has no section %d (file holds only %d); out-of-range save/level -- returning to title",
					         (int)episodeNum, (int)mainLevel, x);
					fprintf(stderr, "error: %s\n", detail);
					crashlog_note("RECOVERED (JE_loadMap: level section out of range)", detail);
					fclose(ep_f);
					mainLevel = 0;   // caller (JE_main) sees mainLevel == 0 and returns to the title screen
					return;
				}
				read_encrypted_pascal_string(s, sizeof(s), ep_f);
				if (s[0] == '*')
				{
					x++;
					s[0] = ' ';
				}
			}

			ESCPressed = false;

			do
			{
				if (gameLoaded)
				{
					fclose(ep_f);

					if (mainLevel == 0)  // if quit itemscreen
						return;          // back to title screen
					else if (endlessMode)  // endless save loaded from within this shop: bail out so JE_main
						return;             // resumes at its outpost (endlessBetweenLevels), not a campaign reload
					else
						goto new_game;
				}

				// Same EOF guard as the section seek above: a section that ends without a ']L' or a
				// jump (truncated/garbled data) would read past EOF into fread_die -> exit(). Recover.
				if (ftell(ep_f) >= ep_end)
				{
					char detail[192];
					snprintf(detail, sizeof(detail),
					         "episode %d section %d ended with no level to load (truncated/garbled data) -- returning to title",
					         (int)episodeNum, (int)mainLevel);
					fprintf(stderr, "error: %s\n", detail);
					crashlog_note("RECOVERED (JE_loadMap: section had no loadable level)", detail);
					fclose(ep_f);
					mainLevel = 0;
					return;
				}

				strcpy(s, " ");
				read_encrypted_pascal_string(s, sizeof(s), ep_f);

				if (qa_net_gameplay_ticks > 0 && s[0] == ']')
				{
					fprintf(stderr, "net gameplay: script ]%c\n", s[1]);
					fflush(stderr);
				}

				if (s[0] == ']')
				{
					switch (s[1])
					{
					case 'A':
						JE_playAnim("tyrend.anm", 0, 7);
						break;

					case 'G':
						mapOrigin = atoi(s + 4);
						mapPNum   = atoi(s + 7);
						for (i = 0; i < mapPNum; i++)
						{
							mapPlanet[i] = atoi(s + 1 + (i + 1) * 8);
							mapSection[i] = atoi(s + 4 + (i + 1) * 8);
						}
						break;

					case '?':
						temp = atoi(s + 4);
						for (i = 0; i < temp; i++)
						{
							cubeList[i] = atoi(s + 3 + (i + 1) * 4);
						}
						if (cubeMax > temp)
							cubeMax = temp;
						break;

					case '!':
						cubeMax = atoi(s + 4);    /*Auto set CubeMax*/
						break;

					case '+':
						temp = atoi(s + 4);
						cubeMax += temp;
						if (cubeMax > 4)
							cubeMax = 4;
						break;

					case 'g':
						galagaMode = true;   /*GALAGA mode*/

						// Solo arms the spawnable wing here. Co-op equipped both ships in ']e'
						// already and flies them like the solo ship: front gun only, no Vulcan.
						if (!coop_mode_active())
						{
							player[1].items = player[0].items;
							player[1].items.weapon[REAR_WEAPON].id = 15;  // Vulcan Cannon
							for (uint i = 0; i < COUNTOF(player[1].items.sidekick); ++i)
								player[1].items.sidekick[i] = 0;          // None
						}
						break;

					case 'x':
						extraGame = true;
						break;

					case 'e': // ENGAGE mode, used for mini-games
						engageMode = true;
						doNotSaveBackup = true;
						constantDie = false;
						onePlayerAction = true;
						superTyrian = true;
						// Online co-op flies the mini-game with both ships; solo drops to the
						// single ship exactly as shipped.
						if (!coop_mode_active())
							twoPlayerMode = false;

						// Co-op issues the mini-game loadout to both players as equals; TIME WAR
						// has no ']g' to copy it onto ship two, so it happens here.
						for (uint p = 0; p < (coop_mode_active() ? 2u : 1u); ++p)
						{
							player[p].cash = 0;

							player[p].items.ship = 13;                     // The Stalker 21.126
							player[p].items.weapon[FRONT_WEAPON].id = 39;  // Atomic RailGun
							player[p].items.weapon[REAR_WEAPON].id = 0;    // None
							for (uint i = 0; i < COUNTOF(player[p].items.sidekick); ++i)
								player[p].items.sidekick[i] = 0;           // None
							player[p].items.generator = 2;                 // Advanced MR-12
							player[p].items.shield = 4;                    // Advanced Integrity Field
							player[p].items.special = 0;                   // None

							player[p].items.weapon[FRONT_WEAPON].power = 3;
							player[p].items.weapon[REAR_WEAPON].power = 1;
						}
						break;

					case 'J':  // section jump
						temp = atoi(s + 3);
						mainLevel = temp;
						jumpSection = true;
						break;

					case '2':  // two-player section jump
						temp = atoi(s + 3);
						if (arcade_rules_active())
						{
							mainLevel = temp;
							jumpSection = true;
						}
						break;

					case 'w':  // Stalker 21.126 section jump
						temp = atoi(s + 3);   /*Allowed to go to Time War?*/
						// A session with two full ships needs two tickets: the Time War door
						// only opens when both are flying the Stalker 21.126.
						if (player[0].items.ship == 13
						    && (!dual_ship_mode() || player[1].items.ship == 13))
						{
							mainLevel = temp;
							jumpSection = true;
						}
						break;

					case 't':
						temp = atoi(s + 3);
						if (levelTimer && levelTimerCountdown == 0)
						{
							mainLevel = temp;
							jumpSection = true;
						}
						break;

					case 'l':
						temp = atoi(s + 3);
						if (!all_players_alive())
						{
							mainLevel = temp;
							jumpSection = true;
						}
						break;

					case 's':
						saveLevel = mainLevel;
						break; /*store savepoint*/

					case 'b':
						// Online rides the 2-player LAST LEVEL slot; solo keeps the original slot 11.
						temp = backup_save_slot();
						// Endless saves at outposts; its mid-level state is not stable.
						if (!endlessMode)
						{
							JE_saveGame(temp, "LAST LEVEL    ");   // drops any stale endless half of the slot
						}
						break;

					case 'i':
						temp = atoi(s + 3);
						songBuy = temp - 1;
						break;

					case 'I': /*Load Items Available Information*/
						// Endless stocks its own outpost and snapshots it after JE_loadMap, so this
						// campaign list would become the stock a bail restores. Read the rows
						// anyway, or the script parser falls out of step with the file.
						if (endlessMode)
						{
							for (int i = 0; i < 9; ++i)
								read_encrypted_pascal_string(s, sizeof(s), ep_f);
							break;
						}

						memset(&itemAvail, 0, sizeof(itemAvail));

						for (int i = 0; i < 9; ++i)
						{
							read_encrypted_pascal_string(s, sizeof(s), ep_f);

							char buf[256];
							strncpy(buf, (strlen(s) > 8) ? s + 8 : "", sizeof(buf));

							int j = 0, temp;
							while (j < (int)COUNTOF(itemAvail[i]) && str_pop_int(buf, &temp))
								itemAvail[i][j++] = temp;
							itemAvailMax[i] = j;
						}

						// Re-offer the DOS Charge-Laser Cannon in its original shops. Tyrian
						// 2000 reused its option slot 16 for the Mint-O-Ship, so it can't ride
						// the stock shop data; inject its re-added slot into the Opt1/Opt2
						// sidekick lists (per originaldostyriandata/LEVELS{2,3}.DAT).
						if (chargeLaserSlot > 0 &&
						    ((episodeNum == 2 && (mainLevel == 3 || mainLevel == 11 || mainLevel == 16)) ||
						     (episodeNum == 3 && mainLevel == 16)))
						{
							if (itemAvailMax[5] < 10) itemAvail[5][itemAvailMax[5]++] = chargeLaserSlot;
							if (itemAvailMax[6] < 10) itemAvail[6][itemAvailMax[6]++] = chargeLaserSlot;
						}

						JE_itemScreen();
						break;

					case 'L':
						if (qa_net_gameplay_ticks > 0)
						{
							fprintf(stderr, "net gameplay: level record read\n");
							fflush(stderr);
						}
						nextLevel = atoi(s + 9);
						SDL_strlcpy(levelName, s + 13, 10);
						levelSong = atoi(s + 22);
						if (nextLevel == 0)
						{
							nextLevel = mainLevel + 1;
						}
						lvlFileNum = atoi(s + 25);
						loadLevelOk = true;
						bonusLevelCurrent = (strlen(s) > 28) & (s[28] == '$');
						normalBonusLevelCurrent = (strlen(s) > 27) & (s[27] == '$');
						gameJustLoaded = false;
						break;

					case '@':
						useLastBank = !useLastBank;
						break;

					case 'Q':
						ESCPressed = false;
						temp = secretHint + (mt_rand() % 3) * 3;

						if (twoPlayerMode)
						{
							for (uint i = 0; i < 2; ++i)
							{
								char label[80];
								JE_playerScoreLabel((JE_byte)(i + 1), label, sizeof(label));
								snprintf(levelWarningText[i], sizeof(*levelWarningText), "%s %lld",
								         label, (long long)player[i].cash);
							}
							strcpy(levelWarningText[2], "");
							levelWarningLines = 3;
						}
						else
						{
							sprintf(levelWarningText[0], "%s %lld", miscText[37], (long long)JE_totalScore(&player[0]));
							strcpy(levelWarningText[1], "");
							levelWarningLines = 2;
						}

						for (x = 0; x < temp - 1; x++)
						{
							do
							{
								read_encrypted_pascal_string(s, sizeof(s), ep_f);
							} while (s[0] != '#');
						}

						do
						{
							read_encrypted_pascal_string(s, sizeof(s), ep_f);
							strcpy(levelWarningText[levelWarningLines], s);
							levelWarningLines++;
						} while (s[0] != '#');
						levelWarningLines--;

						JE_wipeKey();
						frameCountMax = 4;
						if (!constantPlay)
							JE_displayText();

						fade_black(15);

						JE_nextEpisode();

						if (jumpBackToEpisode1 && !arcade_rules_active())
						{
							JE_loadPic(VGAScreen, 1, false); // huh?
							JE_clr256(VGAScreen);

							if (superTyrian)
							{
								// if completed Zinglon's Revenge, show SuperTyrian and Destruct codes
								// if completed SuperTyrian, show Nort-Ship Z code
								superArcadeMode = (initialDifficulty == DIFFICULTY_LORD_OF_GAME) ? 8 : 1;
							}

							if (superArcadeMode < SA_ENGAGE)
							{
								if (SANextShip[superArcadeMode] == SA_ENGAGE)
								{
									sprintf(buffer, "%s %s", miscTextB[4], pName[0]);
									JE_dString(VGAScreen, JE_fontCenter(buffer, FONT_SHAPES), 100, buffer, FONT_SHAPES);

									sprintf(buffer, "Or play... %s", specialName[SA_DESTRUCT - 1]);
									JE_dString(VGAScreen, 80, 180, buffer, SMALL_FONT_SHAPES);
								}
								else
								{
									JE_dString(VGAScreen, JE_fontCenter(superShips[0], FONT_SHAPES), 30, superShips[0], FONT_SHAPES);
									JE_dString(VGAScreen, JE_fontCenter(superShips[SANextShip[superArcadeMode]], SMALL_FONT_SHAPES), 100, superShips[SANextShip[superArcadeMode]], SMALL_FONT_SHAPES);
								}

								if (SANextShip[superArcadeMode] < SA_NORTSHIPZ)
									blit_sprite2x2(VGAScreen, 148, 70, spriteSheet9, ships[SAShip[SANextShip[superArcadeMode]-1]].shipgraphic);
								else if (SANextShip[superArcadeMode] == SA_NORTSHIPZ)
									trentWin = true;

								sprintf(buffer, "Type %s at Title", specialName[SANextShip[superArcadeMode]-1]);
								JE_dString(VGAScreen, JE_fontCenter(buffer, SMALL_FONT_SHAPES), 160, buffer, SMALL_FONT_SHAPES);
								JE_showVGA();

								fade_palette(colors, 50, 0, 255);

								if (!constantPlay)
									wait_input(true, true, true);

								/* trentWin leaves the outer game loop and closes the network session. Keep
								 * the final unlock visible until both players dismiss it. */
								if (isNetworkGame && trentWin)
									network_end_screen_rendezvous(true);
							}

							jumpSection = true;

							if (isNetworkGame)
								JE_readTextSync();

							if (superTyrian)
							{
								fade_black(10);

								// back to titlescreen
								mainLevel = 0;
								return;
							}
						}
						break;

					case 'P':
						if (!constantPlay)
						{
							JE_word tempX = atoi(s + 3);
							if (tempX > 900)
							{
								memcpy(colors, palettes[pcxpal[tempX-1 - 900]], sizeof(colors));
								JE_clr256(VGAScreen);
								JE_showVGA();
								fade_palette(colors, 1, 0, 255);
							}
							else
							{
								if (tempX == 0)
									JE_loadPCX("tshp2.pcx");
								else
									JE_loadPic(VGAScreen, tempX, false);

								JE_showVGA();
								fade_palette(colors, 10, 0, 255);
							}
						}
						break;

					case 'U':
						if (!constantPlay)
						{
							memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

							JE_word tempX = atoi(s + 3);
							JE_loadPic(VGAScreen, tempX, false);
							copy_screen_to_buffer(pic_buffer);

							service_SDL_events(true);

							animate_picture_wipe(WIPE_U, pic_buffer);

							copy_buffer_to_screen(pic_buffer);
						}
						break;

					case 'V':
						if (!constantPlay)
						{
							/* Picture wipes run outside synchronized simulation. */
							memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

							JE_word tempX = atoi(s + 3);
							JE_loadPic(VGAScreen, tempX, false);
							copy_screen_to_buffer(pic_buffer);

							service_SDL_events(true);

							animate_picture_wipe(WIPE_V, pic_buffer);

							copy_buffer_to_screen(pic_buffer);
						}
						break;

					case 'R':
						if (!constantPlay)
						{
							/* Picture wipes run outside synchronized simulation. */
							memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

							JE_word tempX = atoi(s + 3);
							JE_loadPic(VGAScreen, tempX, false);
							copy_screen_to_buffer(pic_buffer);

							service_SDL_events(true);

							animate_picture_wipe(WIPE_R, pic_buffer);

							copy_buffer_to_screen(pic_buffer);
						}
						break;

					case 'C':
						if (!isNetworkGame)
						{
							fade_black(10);
						}
						JE_clr256(VGAScreen);
						JE_showVGA();
						memcpy(colors, palettes[7], sizeof(colors));
						set_palette(colors, 0, 255);
						break;

					case 'B':
						if (!isNetworkGame)
						{
							fade_black(10);
						}
						break;
					case 'F':
						if (!isNetworkGame)
						{
							fade_white(100);
							fade_black(30);
						}
						JE_clr256(VGAScreen);
						JE_showVGA();
						break;

					case 'W':
						if (!constantPlay)
						{
							if (!ESCPressed)
							{
								JE_wipeKey();
								warningCol = 14 * 16 + 5;
								warningColChange = 1;
								warningSoundDelay = 0;
								levelWarningDisplay = (s[2] == 'y');
								levelWarningLines = 0;
								frameCountMax = atoi(s + 4);
								setDelay2(6);
								warningRed = frameCountMax / 10;
								frameCountMax = frameCountMax % 10;

								do
								{
									read_encrypted_pascal_string(s, sizeof(s), ep_f);

									if (s[0] != '#')
									{
										strcpy(levelWarningText[levelWarningLines], s);
										levelWarningLines++;
									}
								} while (!(s[0] == '#'));

								JE_displayText();
								newkey = false;
							}
						}
						break;

					case 'H':
						if (initialDifficulty < DIFFICULTY_HARD)
						{
							mainLevel = atoi(s + 4);
							jumpSection = true;
						}
						break;

					case 'h':
						if (initialDifficulty > DIFFICULTY_NORMAL)
						{
							read_encrypted_pascal_string(s, sizeof(s), ep_f);
						}
						break;

					case 'S':
						if (isNetworkGame)
						{
							JE_readTextSync();
						}
						break;

					case 'n':
						ESCPressed = false;
						break;

					case 'M':
						temp = atoi(s + 3);
						play_song(temp - 1);
						break;

					case 'T':
						if (timedBattleMode)
						{
							// ]T[ 43 44 45 46 47; Episode 1
							// ]T[ 03 03 04 05 06; Episode 5
							mainLevel = atoi(s + (timeBattleSelection * 3));
							jumpSection = true;
						}
						break;

					case 'q':
						if (timedBattleMode)
						{
							if (isNetworkGame)
								JE_timedBattleResult();
							else
								JE_highScoreCheck();
							mainLevel = 0;
							return;
						}
						break;
					}
				}

			} while (!(loadLevelOk || jumpSection));

			fclose(ep_f);

		} while (!loadLevelOk);
	}

	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: level script done\n");
		fflush(stderr);
	}

	if (play_demo)
		load_next_demo();
	else
		fade_black(50);

	/* Return the display to the normal gameplay offset after the fade */
	set_menu_centered(false);

	// The scan above set lvlFileNum from the section's first ']L', but a caller may need a LATER
	// ']L' in the same section (Episode 1 section 3's second TYRIAN cut); reachable via the
	// endless pool or the debug/level-select menu.
	if (forcedLvlFileNum != 0)
	{
		if (JE_levelFileNumValid(forcedLvlFileNum))
			lvlFileNum = forcedLvlFileNum;
		else
			fprintf(stderr, "warning: ignoring missing level file %u in episode %u\n",
			        (unsigned int)forcedLvlFileNum, (unsigned int)episodeNum);
		forcedLvlFileNum = 0;
	}

	if (!JE_levelFileNumValid(lvlFileNum))
	{
		fprintf(stderr, "error: episode %u references missing level file %u (available: 1-%u)\n",
		        (unsigned int)episodeNum, (unsigned int)lvlFileNum, (unsigned int)(lvlNum / 2));
		JE_tyrianHalt(1);
		return;
	}

	FILE* level_f = dir_fopen_die(JE_episodeDir(), levelFile, "rb");
	if (fseek(level_f, lvlPos[(lvlFileNum - 1) * 2], SEEK_SET) != 0)
	{
		fprintf(stderr, "error: failed to seek to episode %u level file %u\n",
		        (unsigned int)episodeNum, (unsigned int)lvlFileNum);
		fclose(level_f);
		JE_tyrianHalt(1);
		return;
	}

	JE_char char_mapFile;
	JE_char char_shapeFile;
	fread_die(&char_mapFile,   1, 1, level_f);
	fread_die(&char_shapeFile, 1, 1, level_f);
	fread_u16_die(&mapX,  1, level_f);
	fread_u16_die(&mapX2, 1, level_f);
	fread_u16_die(&mapX3, 1, level_f);

	fread_u16_die(&levelEnemyMax, 1, level_f);
	if (levelEnemyMax > COUNTOF(levelEnemy))
	{
		fprintf(stderr, "error: episode %u level file %u has too many random enemies (%u)\n",
		        (unsigned int)episodeNum, (unsigned int)lvlFileNum, (unsigned int)levelEnemyMax);
		fclose(level_f);
		JE_tyrianHalt(1);
		return;
	}
	fread_u16_die(levelEnemy, levelEnemyMax, level_f);

	fread_u16_die(&maxEvent, 1, level_f);
	if (maxEvent >= COUNTOF(eventRec))
	{
		fprintf(stderr, "error: episode %u level file %u has too many events (%u)\n",
		        (unsigned int)episodeNum, (unsigned int)lvlFileNum, (unsigned int)maxEvent);
		fclose(level_f);
		JE_tyrianHalt(1);
		return;
	}
	for (x = 0; x < maxEvent; x++)
	{
		fread_u16_die(&eventRec[x].eventtime, 1, level_f);
		fread_u8_die( &eventRec[x].eventtype, 1, level_f);
		fread_s16_die(&eventRec[x].eventdat,  1, level_f);
		fread_s16_die(&eventRec[x].eventdat2, 1, level_f);
		fread_s8_die( &eventRec[x].eventdat3, 1, level_f);
		fread_s8_die( &eventRec[x].eventdat5, 1, level_f);
		fread_s8_die( &eventRec[x].eventdat6, 1, level_f);
		fread_u8_die( &eventRec[x].eventdat4, 1, level_f);
	}
	eventRec[x].eventtime = 65500;  /* Sentinel event. */
	event_backdate_savara_cannons();

	/* Map shape lookup table; each map follows its level data. */
	for (temp = 0; temp < 3; temp++)
	{
		fread_u16_die(mapSh[temp], sizeof(*mapSh) / sizeof(JE_word), level_f);
		for (temp2 = 0; temp2 < 128; temp2++)
		{
			mapSh[temp][temp2] = SDL_Swap16(mapSh[temp][temp2]);
		}
	}

	/* Read Shapes.DAT */
	sprintf(tempStr, "shapes%c.dat", tolower((unsigned char)char_shapeFile));
	FILE *shpFile = dir_fopen_die(data_dir(), tempStr, "rb");

	for (int z = 0; z < 600; z++)
	{
		JE_boolean shapeBlank;
		fread_bool_die(&shapeBlank, shpFile);

		if (shapeBlank)
			memset(shape, 0, sizeof(shape));
		else
			fread_u8_die(shape, sizeof(shape), shpFile);

		/* Match 1 */
		for (int x = 0; x <= 71; ++x)
		{
			if (mapSh[0][x] == z+1)
			{
				memcpy(megaData1.shapes[x].sh, shape, sizeof(JE_DanCShape));

				ref[0][x] = megaData1.shapes[x].sh;
			}
		}

		/* Match 2 */
		for (int x = 0; x <= 71; ++x)
		{
			if (mapSh[1][x] == z+1)
			{
				if (x != 71 && !shapeBlank)
				{
					memcpy(megaData2.shapes[x].sh, shape, sizeof(JE_DanCShape));

					y = 1;
					for (yy = 0; yy < (24 * 28) >> 1; yy++)
						if (shape[yy] == 0)
							y = 0;

					megaData2.shapes[x].fill = y;
					ref[1][x] = megaData2.shapes[x].sh;
				}
				else
				{
					ref[1][x] = NULL;
				}
			}
		}

		/*Match 3*/
		for (int x = 0; x <= 71; ++x)
		{
			if (mapSh[2][x] == z+1)
			{
				if (x < 70 && !shapeBlank)
				{
					memcpy(megaData3.shapes[x].sh, shape, sizeof(JE_DanCShape));

					y = 1;
					for (yy = 0; yy < (24 * 28) >> 1; yy++)
						if (shape[yy] == 0)
							y = 0;

					megaData3.shapes[x].fill = y;
					ref[2][x] = megaData3.shapes[x].sh;
				}
				else
				{
					ref[2][x] = NULL;
				}
			}
		}
	}

	fclose(shpFile);

	fread_u8_die(mapBuf, 14 * 300, level_f);
	bufLoc = 0;              /* Map 1. */
	for (y = 0; y < 300; y++)
	{
		for (x = 0; x < 14; x++)
		{
			megaData1.mainmap[y][x] = ref[0][mapBuf[bufLoc]];
			bufLoc++;
		}
	}

	fread_u8_die(mapBuf, 14 * 600, level_f);
	bufLoc = 0;              /* Map 2. */
	for (y = 0; y < 600; y++)
	{
		for (x = 0; x < 14; x++)
		{
			megaData2.mainmap[y][x] = ref[1][mapBuf[bufLoc]];
			bufLoc++;
		}
	}

	fread_u8_die(mapBuf, 15 * 600, level_f);
	bufLoc = 0;              /* Map 3. */
	for (y = 0; y < 600; y++)
	{
		for (x = 0; x < 15; x++)
		{
			megaData3.mainmap[y][x] = ref[2][mapBuf[bufLoc]];
			bufLoc++;
		}
	}

	fclose(level_f);

	/* Map shape pointers use (MAPSH - 1) * 168 plus S2Ofs. */
	/* End of find loop for LEVEL??.DAT */
}

#ifdef WITH_NETWORK
/* Start a fresh online Endless run. Everything it needs came over in the connect packet, so both
 * machines build the same run without exchanging anything further. */
static void networkEndlessNewRun(void)
{
	endlessResetRun();
	endlessSetSeed(network_endless_session_seed);
	endlessRunMode = (EndlessRunMode)network_host_endless_run_mode;
	endlessCourseChooser = (EndlessCourseChooser)network_host_endless_chooser;
	endlessCoopComboShared = network_host_endless_combo_shared;
	endlessRunBaseRule = (EndlessBaseRule)network_host_endless_base_rule;
	endlessRecordRunStart();

	endlessMode = true;
	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		player[i].is_dragonwing = false;
		player[i].lives = &player[i].items.weapon[player_lives_port(i)].power;
		player[i].items = player[0].items;
		player[i].last_items = player[0].last_items;
		player[i].cash = 0;
	}

	// The stake is the run's first income for each ship, booked so earned - spent stays true.
	endlessCashResync();
	endlessCashCredit(endlessStartingCash(), ENDLESS_CASH_START);
	player[endlessPartnerIndex()].cash = player[endlessEconomyIndex()].cash;

	endlessApplyStartingLoadout();
	endlessReseedPlayers(0);
}

/* Resume an online Endless run: the host loads it from its own save file and streams the
 * record, and the joiner adopts it. */
static bool networkEndlessResume(JE_byte slot)
{
	const Uint32 begin = SDL_GetTicks();

	bool okay;
	if (thisPlayerNum == networkHostPlayerNum)
	{
		okay = endlessLoadSlot(slot);
		if (okay)
			network_endless_run_publish();
	}
	else
	{
		okay = network_endless_run_receive(20000);
	}

	// A bounded wait that succeeds late leaves no trace. Put a number on the handoff so a
	// slow resume is attributable from the net log alone.
	const Uint32 spent = SDL_GetTicks() - begin;
	if (okay && spent > 3000)
	{
		char detail[120];
		snprintf(detail, sizeof(detail),
		         "the %s spent %lu ms in the run handoff before the outpost could open.",
		         thisPlayerNum == networkHostPlayerNum ? "host (sidecar load + transfer)"
		                                              : "joiner (transfer wait + adopt)",
		         (unsigned long)spent);
		crashlog_netlog_line("ENDLESS RESUME SLOW", detail);
	}

	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: endless resume %s, slot %d\n",
		        okay ? "ok" : "FAILED", (int)slot);
		fflush(stderr);
	}

	return okay;
}

/* Steps the host adds to the lobby's difficulty, which the joiner subtracts again so both land
 * on the same initialDifficulty. */
int networkDifficultyBump(void)
{
	return dual_ship_mode() ? 0 : 1;
}

/* Let each player choose an independent Super Arcade ship while servicing the connection. Returns
 * 1..SA after both picks settle, or 0 if the session ends. */
static int networkSuperArcadeShipSelect(void)
{
	// Command-line netplay reaches here without the title screen having loaded these.
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	network_sa_ship_reset();

	int selected = 0;                 // index into SAShip, 0..SA-1
	int chosen = 0;                   // 1..SA once this player has committed
	int peer = 0;
	bool restart = true;
	int wName[SA] = { 0 };
	// What this machine last put on the wire, so a change is announced exactly once. The second
	// half says the peer's pick is in hand; publishing it is what lets THEM stop offering the
	// take-back and leave (see network_sa_ship_publish).
	int announcedPick = 0;
	bool announcedSawPeer = false;
	// Wait for the press that opened this screen to be released before accepting a ship.
	bool armed = false;
	const Uint32 armDeadline = SDL_GetTicks() + 500;
	// This screen redraws every frame (it has to: the peer's pick can land at any moment), so
	// hover is re-evaluated only on real motion, or a parked cursor would veto the arrow keys.
	Sint32 lastSeenMouseX = mouse_x, lastSeenMouseY = mouse_y;

	// Wire runs have no keyboard: take the slot's own ship so the two peers differ on purpose.
	if (qa_net_gameplay_ticks > 0)
		chosen = (thisPlayerNum == 2) ? 2 : 1;

	while (true)
	{
		if (restart)
		{
			JE_loadPic(VGAScreen2, 2, false);
			// Use the shop palette shared by the raw-index ship sprites.
			memcpy(colors, palettes[0], sizeof(colors));
			for (int y = 0; y < VGAScreen2->h; ++y)
			{
				Uint8 *const row = (Uint8 *)VGAScreen2->pixels + y * VGAScreen2->pitch;
				for (int x = 0; x < VGAScreen2->w; ++x)
				{
					const Uint8 dim = (row[x] & 0xf0) | ((row[x] & 0x0f) >> 3);
					const SDL_Color c = colors[dim];
					row[x] = (c.r + c.g + c.b > 96) ? 0 : dim;
				}
			}
			draw_font_hv_shadow(VGAScreen2, 320 / 2, SA_PICK_HEADER_Y, superShips[0], large_font,
			                    centered, 15, -3, false, 2);
		}
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Nine names in two columns; the pick is the player's own, so the partner's is only
		// reported, never enforced.
		for (int i = 0; i < SA; ++i)
		{
			const bool onIt = (i == selected);
			wName[i] = JE_textWidth(superShips[i + 1], small_font);
			// small_font sits low in bank 15, so its offsets run positive, as the lobby's rows do;
			// the negative ones the wider menu fonts want bury it in the bank's near-black end.
			// The 1px shadow is that font's too: 2 swallows a 6px glyph.
			draw_font_hv_shadow(VGAScreen, sa_pick_name_x(i), sa_pick_name_y(i), superShips[i + 1],
			                    small_font, left_aligned, 15,
			                    2 + (onIt ? 4 : 0) + (chosen == i + 1 ? 2 : 0), false, 1);
		}

		// The highlighted hull, so a name that means nothing yet still shows what it flies.
		const JE_word gr = ships[SAShip[selected]].shipgraphic;
		if (gr > 500)
			blit_sprite2x2(VGAScreen, 320 / 2 - 12, SA_PICK_SHIP_Y, spriteSheetT2000, gr - 500);
		else if (gr == 1)
		{
			// Nort Ship: shipgraphic 1 is a sentinel, so its two halves are blitted by hand.
			blit_sprite2x2(VGAScreen, 320 / 2 - 24, SA_PICK_SHIP_Y, spriteSheet9, 220);
			blit_sprite2x2(VGAScreen, 320 / 2,      SA_PICK_SHIP_Y, spriteSheet9, 222);
		}
		else
			blit_sprite2x2(VGAScreen, 320 / 2 - 12, SA_PICK_SHIP_Y, spriteSheet9, gr);

		const char *status = chosen == 0
		                   ? "Choose your ship."
		                   : (peer == 0 ? "Waiting for the other player..." : "Both ready.");
		draw_font_hv_shadow(VGAScreen, 320 / 2, SA_PICK_STATUS_Y, status, small_font, centered, 15, 6, false, 1);
		// The partner's pick, as soon as it lands: it changes nothing here, but a player who
		// chose first should see something happen when the other one commits.
		if (peer != 0)
		{
			char line[64];
			snprintf(line, sizeof(line), "Player %u flies %s", thisPlayerNum == 2 ? 1u : 2u, superShips[peer]);
			draw_font_hv_shadow(VGAScreen, 320 / 2, SA_PICK_PEER_Y, line, small_font, centered, 15, 4, false, 1);
		}
		else if (chosen != 0)
		{
			// The same line carries the way out while the wait is the only thing happening.
			draw_font_hv_shadow(VGAScreen, 320 / 2, SA_PICK_PEER_Y, SA_PICK_UNPICK_HINT,
			                    small_font, centered, 15, 2, false, 1);
		}

		// Every other menu screen fades in on its first composed frame; this one reaches here
		// straight out of the handshake's fade_black, so without it the whole screen is drawn
		// under a black palette and the player sees nothing at all.
		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		// Paced like every other menu: vsync-on paces through JE_showVGA itself, and off it
		// follows the render-fps cap. The cursor is an 8-bit sprite composited into the frame, so
		// it only moves when one is presented -- a flat 16ms delay walked it against the display.
		if (!output_vsync)
			limit_render_fps();
		if (restart)
		{
			fade_palette(colors, 10, 0, 255);
			restart = false;
		}

		// A console has no keyboard: every other interactive screen turns the pad into arrows,
		// Return and Escape here, and without it this one is dead on the Switch and the Vita.
		push_joysticks_as_keyboard();
		service_SDL_events(false);
		if (!armed)
		{
			armed = (!newkey && !newmouse && !joydown && !mousedown) || SDL_GetTicks() >= armDeadline;
			newkey = false;
			newmouse = false;
		}
		network_check();
		peer = network_sa_ship_peer();

		// Announce a changed pick, and the moment the peer's arrives. One in flight at a time: the
		// reliable queue is sixteen deep, and a player alternating pick and take-back would
		// otherwise fill it faster than the other machine retires it.
		if ((chosen != announcedPick || (peer != 0) != announcedSawPeer) && network_is_sync())
		{
			announcedPick = chosen;
			announcedSawPeer = (peer != 0);
			network_sa_ship_publish(announcedPick, announcedSawPeer);
		}

		// Both have a ship, and the peer has said they hold ours -- so neither of us is still
		// offering the take-back, and the pair we leave with is the pair they leave with.
		if (chosen != 0 && peer != 0 && network_sa_ship_peer_saw_us()
		    && announcedSawPeer && network_is_sync())
			return chosen;
		if (!network_peer_alive())
			return 0;

		// A committed pick can be taken back for as long as the partner has not made one. Once
		// they have, the pair is settled and the screen is already on its way out.
		const bool canPick = chosen == 0;
		const bool canUnpick = chosen != 0 && peer == 0;

		// Hover and click, like every other ship or episode menu.
		const bool mouseMoved = mouse_x != lastSeenMouseX || mouse_y != lastSeenMouseY;
		lastSeenMouseX = mouse_x;
		lastSeenMouseY = mouse_y;
		if (canPick && (newmouse || mouseMoved))
		{
			for (int i = 0; i < SA; ++i)
			{
				const int x = sa_pick_name_x(i), y = sa_pick_name_y(i);
				if (mouse_x < x || mouse_x >= x + wName[i] || mouse_y < y || mouse_y >= y + SA_PICK_ROW_H)
					continue;

				if (selected != i)
				{
					JE_playSampleNum(S_CURSOR);
					selected = i;
				}
				if (newmouse && lastmouse_but == SDL_BUTTON_LEFT
				    && lastmouse_x >= x && lastmouse_x < x + wName[i]
				    && lastmouse_y >= y && lastmouse_y < y + SA_PICK_ROW_H)
				{
					JE_playSampleNum(S_SELECT);
					chosen = i + 1;
				}
				break;
			}
			newmouse = false;
		}
		// Right-click takes a pick back, the same as Escape does below.
		if (canUnpick && newmouse && lastmouse_but == SDL_BUTTON_RIGHT)
		{
			JE_playSampleNum(S_SPRING);
			chosen = 0;
			newmouse = false;
		}

		// Escape is the one key that means something in both states, so it is read first.
		if (newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
		{
			if (canUnpick)
			{
				// Back out of the pick, not out of the session: the partner has not chosen yet,
				// so there is still time to change ships. The cursor is already on the ship that
				// was picked, so it is left there to move off.
				JE_playSampleNum(S_SPRING);
				chosen = 0;
			}
			else if (chosen == 0)
			{
				// Leaving before committing ends the session; the partner is sitting on this same
				// screen and has to be told, or it waits out the dead-link timeout.
				JE_playSampleNum(S_SPRING);
				network_prepare(PACKET_QUIT);
				network_send(4);  // PACKET QUIT
				network_tyrian_halt(0, true);   // does not return
			}
			// Otherwise both have picked and the game is starting, so there is nothing to leave.
			newkey = false;
		}
		else if (canPick && newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				JE_playSampleNum(S_CURSOR);
				selected = (selected == 0) ? SA - 1 : selected - 1;
				break;

			case SDL_SCANCODE_DOWN:
				JE_playSampleNum(S_CURSOR);
				selected = (selected == SA - 1) ? 0 : selected + 1;
				break;

			case SDL_SCANCODE_LEFT:
			case SDL_SCANCODE_RIGHT:
				JE_playSampleNum(S_CURSOR);
				selected = (selected + SA_PICK_ROWS) % SA;   // hop between the two columns
				break;

			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_KP_ENTER:
			case SDL_SCANCODE_SPACE:
				JE_playSampleNum(S_SELECT);
				chosen = selected + 1;
				break;

			default:
				break;
			}
			newkey = false;
		}
	}
}

/* Equip one ship for the Super Arcade run it chose. Mirrors newSuperArcadeGame's loadout, and
 * records the ship on the ship (player_sa_ship) so the balls, the paired special and the save
 * record all read the right arsenal for each player independently. */
void networkSuperArcadeEquip(Player *this_player, int ship)
{
	const uint i = (uint)ship - 1;

	this_player->items.ship = SAShip[i];
	this_player->items.super_arcade_mode = (Uint8)ship;
	this_player->items.weapon[FRONT_WEAPON].id = SAWeapon[i][0];
	this_player->items.weapon[REAR_WEAPON].id = 0;   // Super Arcade issues no rear gun
	this_player->items.special = SASpecialWeapon[i];
	if (ship == SA_NORTSHIPZ)
		for (uint s = 0; s < COUNTOF(this_player->items.sidekick); ++s)
			this_player->items.sidekick[s] = 24;  // Companion Ship Quicksilver
	this_player->last_items = this_player->items;
}

/* Equip one ship for an online SuperTyrian run. Mirrors newSuperTyrianGame's loadout, and clears
 * the rear bay rather than trusting the slot it lands in: ship two's fresh-game arsenal is the
 * linked pair's Dragonwing one, which arrives carrying a rear Vulcan Cannon. */
void networkSuperTyrianEquip(Player *this_player)
{
	this_player->items.ship = 13;                     // The Stalker 21.126
	this_player->items.weapon[FRONT_WEAPON].id = 39;  // Atomic RailGun
	this_player->items.weapon[REAR_WEAPON].id = 0;    // SuperTyrian issues no rear gun
	this_player->items.super_arcade_mode = SA_SUPERTYRIAN;
	this_player->last_items = this_player->items;
}

/* Both Timed Battle players confirm a start card before scoring begins. The
 * shared screen keeps the link alive until either confirmation or Escape. */
static void networkTimedBattleReady(void)
{
	bool localReady = false, peerReady = false;

	// Nothing counts until the press that opened this screen is let go: a key still down from the
	// menu behind it would confirm the card before it is on screen, and a held pad button
	// auto-repeats into fresh presses.
	bool armed = false;
	Uint32 armDeadline = SDL_GetTicks() + 500;

	// A headless wire peer has nobody to press anything; it is ready as soon as it arrives.
	if (qa_net_gameplay_ticks > 0)
	{
		localReady = true;
		network_ready_publish(true);
	}

	JE_loadPic(VGAScreen2, 2, false);
	draw_font_hv_shadow(VGAScreen2, 320 / 2, 20, "Timed Battle", large_font, centered, 15, -3, false, 2);
	draw_font_hv_shadow(VGAScreen2, 320 / 2, 48, timed_battle_name[timeBattleSelection], normal_font,
	                    centered, 15, -3, false, 2);

	char line[128];
	snprintf(line, sizeof(line), "%s   -   You fly as player %u",
	         difficultyNameB[initialDifficulty], thisPlayerNum);
	draw_font_hv_shadow(VGAScreen2, 320 / 2, 72, line, small_font, centered, 15, 2, false, 1);
	if (network_opponent_name[0] != '\0')
	{
		snprintf(line, sizeof(line), "Racing %s", network_opponent_name);
		draw_font_hv_shadow(VGAScreen2, 320 / 2, 84, line, small_font, centered, 15, 2, false, 1);
	}
	draw_font_hv_shadow(VGAScreen2, 320 / 2, 108, "The clock is the level. Whoever banks the most",
	                    small_font, centered, 15, 4, false, 1);
	draw_font_hv_shadow(VGAScreen2, 320 / 2, 120, "cash before it runs out wins the timed battle.",
	                    small_font, centered, 15, 4, false, 1);

	bool faded = false;

	for (;;)
	{
		// Expose ready and Back before presenting the first frame.
		touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);

		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Where the pair stands, brightened as each side commits: the wait has no timeout, so the
		// screen has to say which of the two it is still waiting on.
		draw_font_hv_shadow(VGAScreen, 320 / 2, 150,
		                    localReady ? "You are ready." : "Press a key when you are ready.",
		                    small_font, centered, 15, localReady ? 6 : 2, false, 1);
		draw_font_hv_shadow(VGAScreen, 320 / 2, 162,
		                    peerReady ? "The other player is ready." : "Waiting for the other player...",
		                    small_font, centered, 15, peerReady ? 6 : 2, false, 1);
		// Esc is a step back rather than a way out while this player stands confirmed, so it takes
		// two presses to leave from there. Says which one it is about to be.
		draw_font_hv_shadow(VGAScreen, 320 / 2, 178,
		                    localReady ? "Esc takes back your ready." : "Esc leaves the session.",
		                    small_font, centered, 15, 0, false, 1);

		JE_showVGA();
		if (!faded)
		{
			fade_palette(colors, 10, 0, 255);
			faded = true;
		}
		if (!output_vsync)
			limit_render_fps();

		watchdog_heartbeat();
		push_joysticks_as_keyboard();  // a controller confirms too (no keyboard on the consoles)
		service_SDL_events(false);

		if (!armed)
		{
			armed = (!newkey && !joydown) || (armDeadline != 0 && SDL_GetTicks() >= armDeadline);
			newkey = false;
		}
		else if (newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
		{
			newkey = false;

			if (localReady)
			{
				// Step back rather than out.
				localReady = false;
				network_ready_publish(false);
				armed = false;
				armDeadline = 0;
			}
			else
			{
				// The way out, once nothing is standing behind it. The other player is sitting on
				// this same screen and has to be told, or they wait out the dead-link timeout
				// instead of being told the session ended.
				network_prepare(PACKET_QUIT);
				network_send(4);  // PACKET_QUIT
				network_tyrian_halt(0, true);   // does not return
			}
		}
		else if (!localReady && newkey)
		{
			localReady = true;
			newkey = false;
			network_ready_publish(true);
		}

		const int peer = network_ready_peer();   // doubles as this frame's keep-alive
		if (peer >= 0)
			peerReady = (peer != 0);

		// Not until our own announcement is acknowledged: leaving with it unretired puts a
		// retransmit in front of the state stream on the very first tick.
		if ((localReady && peerReady && network_is_sync()) || !network_peer_alive())
			break;
	}

	newkey = false;
	fade_black(10);
}

/* Builds the joiner's wait-screen rows. Also used by layout tests. */
int networkGuestWaitRows(const char **label, const char **value)
{
	const bool endless = network_game_type == NETWORK_GAME_ENDLESS;
	const bool coop = endless || network_game_type == NETWORK_GAME_CAMPAIGN;
	const bool superTyrianGame = network_game_type == NETWORK_GAME_SUPERTYRIAN;

	int rows = 0;

	label[rows] = "Host";
	value[rows++] = network_opponent_name[0] ? network_opponent_name : "(unnamed)";
	label[rows] = "Game Type";
	switch (network_game_type)
	{
	case NETWORK_GAME_ENDLESS:      value[rows++] = "Endless";      break;
	case NETWORK_GAME_CAMPAIGN:     value[rows++] = "Campaign";     break;
	case NETWORK_GAME_SUPERTYRIAN:  value[rows++] = "SuperTyrian";  break;
	case NETWORK_GAME_SUPERARCADE:  value[rows++] = "Super Arcade"; break;
	// Destruct never reaches this screen; the remaining cases are Arcade.
	default:  value[rows++] = timedBattleMode ? "Timed Battle" : "Arcade";  break;
	}
	if (timedBattleMode)
	{
		// Match the lobby's Mode/Level labels.
		label[rows] = "Level";
		value[rows++] = timed_battle_name[timeBattleSelection];
	}
	else if (!endless)
	{
		label[rows] = "Episode";
		if (network_host_custom_file[0] != '\0')
		{
			// Fall back to the advertised file name before download.
			static char customShown[28];
			const int idx = customEpisodeFindByFile(network_host_custom_file);
			const char *const name = (idx >= 0 && customEpisodeTitle(idx)[0] != '\0')
			                       ? customEpisodeTitle(idx) : network_host_custom_file;
			SDL_strlcpy(customShown, name, sizeof(customShown));
			if (strlen(name) >= sizeof(customShown))
				memcpy(&customShown[sizeof(customShown) - 4], "...", 4);
			value[rows++] = customShown;
		}
		else
			value[rows++] = episode_name[network_host_episode];
	}
	// SuperTyrian uses this field for its two variants.
	label[rows] = superTyrianGame ? "Variant" : "Difficulty";
	value[rows++] = superTyrianGame
	              ? (network_host_difficulty == DIFFICULTY_SUICIDE ? "Scrollock" : "Standard")
	              : difficultyNameB[network_host_difficulty];
	if (endless)
	{
		label[rows] = "Run Mode";
		value[rows++] = endlessRunModeName((EndlessRunMode)network_host_endless_run_mode);
		if (network_host_custom_endless != CUSTOM_ENDLESS_OFF)
		{
			// The host supplies this value with its collection.
			label[rows] = "Custom Levels";
			value[rows++] = network_host_custom_endless == CUSTOM_ENDLESS_ONLY
			              ? "Custom Only" : "Mixed";
		}
		label[rows] = "Base Level";
		value[rows++] = endlessBaseLevelRuleName(network_host_endless_base_rule);
		label[rows] = "Charts Course";
		value[rows++] = endlessCourseChooserName((EndlessCourseChooser)network_host_endless_chooser);
		label[rows] = "Combo Feed";
		value[rows++] = network_host_endless_combo_shared ? "Shared" : "Individual";
		label[rows] = "Seed";
		value[rows++] = network_endless_session_seed[0] ? network_endless_session_seed : "(random)";
	}
	if (coop)
	{
		label[rows] = "Credit";
		value[rows++] = coop_credit_is_shared() ? "Shared" : "Individual";
		if (!coop_credit_is_shared())
		{
			label[rows] = "Double Earnings";
			value[rows++] = coop_earnings_are_doubled() ? "On" : "Off";
		}
	}
	else if (network_game_type == NETWORK_GAME_SUPERARCADE)
	{
		label[rows] = "Ships";
		value[rows++] = "You choose";   // each player picks their own on the next screen
	}
	else if (arcadeSeparateMode)
	{
		label[rows] = "Ships";
		value[rows++] = "Separate";
	}
	else
	{
		// Campaign gives both slots the same ship type.
		label[rows] = "You Fly";
		value[rows++] = thisPlayerNum == 2 ? "Dragonwing" : "Silver Ship";
	}
	label[rows] = "Game Speed";
	// Clamp again before indexing the display table.
	value[rows++] = gameSpeedText[MIN(MAX(gameSpeed, 1), 5) - 1];
	label[rows] = "Netcode";
	value[rows++] = nrb_session_mode() ? "Rollback" : "Delay-Based";
	if (nrb_session_mode())
	{
		// Lockstep has no desync-recovery state.
		label[rows] = "Desync Recovery";
		value[rows++] = nrb_session_recovery() ? "On" : "Off";
	}

	return rows;
}

static void networkCustomSyncScreen(void)
{
	JE_clr256(VGAScreen);
	draw_font_hv_shadow(VGAScreen, 320 / 2, 90, "Syncing custom levels...",
	                    normal_font, centered, 15, -2, false, 2);
	JE_showVGA();
	fade_palette(colors, 10, 0, 255);
}

void networkStartScreen(void)
{
	// Lobby games are already connected. Command-line netplay and its lobby-settings
	// wire test still connect here.
	if (!network_from_lobby || (qa_net_gameplay_ticks > 0 && qa_net_lobby_settings))
	{
		JE_loadPic(VGAScreen, 2, false);
		draw_font_hv_shadow(VGAScreen, 320 / 2, 20, "Online Multiplayer", large_font, centered, 15, -3, false, 2);
		memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);
		JE_dString(VGAScreen, JE_fontCenter("Waiting for other player.", SMALL_FONT_SHAPES), 140, "Waiting for other player.", SMALL_FONT_SHAPES);
		JE_showVGA();
		fade_palette(colors, 10, 0, 255);

		network_connect();
	}
	else
	{
		// Already connected, but the joiner's wait screen below composes itself by copying
		// VGAScreen2, so it still needs the backdrop staged there; otherwise it draws over
		// whatever the lobby happened to leave behind.
		JE_loadPic(VGAScreen2, 2, false);
		draw_font_hv_shadow(VGAScreen2, 320 / 2, 20, "Online Multiplayer", large_font, centered, 15, -3, false, 2);
	}

	// Destruct receives its mode, teams, and terrain seed from the lobby. Skip the campaign
	// handshake and wait for the reliable channel before entering the minigame.
	if (network_game_type == NETWORK_GAME_DESTRUCT)
	{
		while (!network_is_sync())
		{
			service_SDL_events(false);

			mouseCursor = MOUSE_POINTER_NORMAL;
			JE_mouseStart();
			JE_showVGA();
			JE_mouseReplace();
			if (!output_vsync)
				limit_render_fps();

			network_check();
		}

		fade_black(10);
		loadDestruct = true;
		return;
	}

	twoPlayerMode = true;
	coopCampaignMode = network_game_type == NETWORK_GAME_CAMPAIGN;
	coopEndlessMode = network_game_type == NETWORK_GAME_ENDLESS;
	/* Arcade's Timed Battle: two personal arcades racing one battle level for cash, so it is the
	 * Separate shape with the mode flag the level scripts read (event 84's clock, event 85's
	 * hatching enemies, and the ']T' jump that picks the section out of the episode). */
	timedBattleMode = network_timed_battle();
	if (timedBattleMode)
	{
		timeBattleSelection = (JE_byte)network_host_battle_level;
		arcadeSeparateMode = true;
	}
	/* The two one-player rulesets flown online give each player a complete ship, so they run in
	 * the Separate arcade shape whatever the host's Mode row says. */
	if (network_game_type_is_super(network_game_type))
		arcadeSeparateMode = true;
	bool resumed = false;
	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: connected, player %u\n", thisPlayerNum);
		fflush(stderr);
	}
	if (thisPlayerNum == networkHostPlayerNum)
	{
		fade_black(10);

		// New Game or Load Game. A load applies the save right in the load menu; its record rides in
		// the details packet so the joiner adopts the exact same state (difficulty already carries
		// the 2-player +1 bump).
		const int resumeSlot = timedBattleMode ? 0 : networkHostStartSelect();
		if (resumeSlot > 0)
		{
			// The load clears the co-op flags; the game type the pair connected on owns them, and
			// the seat belongs to the save rather than to the lobby row that opened the session.
			// See doc/notes.md#online-saves; the joiner reasserts both the same way.
			coopCampaignMode = network_game_type == NETWORK_GAME_CAMPAIGN;
			coopEndlessMode = network_game_type == NETWORK_GAME_ENDLESS;

			networkHostPlayerNum = save_slot_online_player((JE_byte)resumeSlot);
			thisPlayerNum = networkHostPlayerNum;

			// A resumed save overrides the lobby's custom-episode selection.
			if (customEpisodeActive())
			{
				customEpisodeScan();
				const int customIdx = customEpisodeFindByFile(customEpisodeActiveFile());
				SDL_strlcpy(network_host_custom_file, customEpisodeActiveFile(),
				            sizeof(network_host_custom_file));
				if (customIdx < 0 ||
				    !customEpisodeIdentity(customIdx, &network_host_custom_size,
				                           &network_host_custom_hash))
					network_tyrian_halt(3, false);
			}
			else
			{
				network_host_custom_file[0] = '\0';
				network_host_custom_size = 0;
				network_host_custom_hash = 0;
			}

			network_prepare(PACKET_DETAILS);
			SDLNet_Write16(network_game_type, &packet_out_temp->data[4]);
			SDLNet_Write16(episodeNum, &packet_out_temp->data[6]);
			SDLNet_Write16(difficultyLevel, &packet_out_temp->data[8]);
			save_record_pack(&packet_out_temp->data[10], &saveFiles[resumeSlot - 1]);
			packet_out_temp->data[10 + SAVE_RECORD_PACKED_SIZE] = (Uint8)networkHostPlayerNum;
			// A stock resume sends a zeroed custom-container identity.
			SDLNet_Write32(network_host_custom_size,
			               &packet_out_temp->data[11 + SAVE_RECORD_PACKED_SIZE]);
			SDLNet_Write32(network_host_custom_hash,
			               &packet_out_temp->data[15 + SAVE_RECORD_PACKED_SIZE]);
			network_send(19 + SAVE_RECORD_PACKED_SIZE);  // PACKET_DETAILS (resume form)

			if (customEpisodeActive() ||
			    (coopEndlessMode && network_host_custom_endless != CUSTOM_ENDLESS_OFF))
				networkCustomSyncScreen();

			if (customEpisodeActive() && !network_custom_level_serve())
				network_tyrian_halt(3, false);

			if (coopEndlessMode && network_host_custom_endless != CUSTOM_ENDLESS_OFF &&
			    !network_custom_endless_serve())
				network_tyrian_halt(3, false);

			// The save record carries the two loadouts; the Endless run behind them is a record of its
			// own, so it follows on the reliable channel before either machine plays a tick.
			if (coopEndlessMode && !networkEndlessResume((JE_byte)resumeSlot))
				network_tyrian_halt(3, false);

			resumed = true;
		}
		else if (resumeSlot == 0)
		{
			const bool customSession = network_host_custom_file[0] != '\0' &&
			                           !coopEndlessMode && !timedBattleMode;
			// Endless traverses episodes as it deepens, so it always opens on the first one, and a
			// battle level is reached through the episode that holds it (the ']T' jump list).
			if (customSession)
			{
				// The selected file may have disappeared since the lobby scan.
				if (!networkCustomEpisodeActivate())
					network_tyrian_halt(3, false);
			}
			else
			{
				JE_initEpisode(coopEndlessMode ? 1
				               : timedBattleMode ? (JE_byte)network_timed_battle_episode(timeBattleSelection)
				               : network_host_episode);
			}
			/* A lobby row picked the episode, so the episode-select menu that normally records
			 * where a run began never ran. The co-op Campaign board and the save record both
			 * read it, and a value left over from an earlier game names the wrong episode. */
			initial_episode_num = episodeNum;
			difficultyLevel = network_host_difficulty;
			initialDifficulty = difficultyLevel;

			difficultyLevel += networkDifficultyBump();

			network_prepare(PACKET_DETAILS);
			SDLNet_Write16(network_game_type, &packet_out_temp->data[4]);
			SDLNet_Write16(episodeNum, &packet_out_temp->data[6]);
			SDLNet_Write16(difficultyLevel, &packet_out_temp->data[8]);
			network_send(10);  // PACKET_DETAILS

			if (customSession ||
			    (coopEndlessMode && network_host_custom_endless != CUSTOM_ENDLESS_OFF))
				networkCustomSyncScreen();

			if (customSession && !network_custom_level_serve())
				network_tyrian_halt(3, false);

			if (coopEndlessMode && network_host_custom_endless != CUSTOM_ENDLESS_OFF &&
			    !network_custom_endless_serve())
				network_tyrian_halt(3, false);
		}
		else
		{
			network_prepare(PACKET_QUIT);
			network_send(4);  // PACKET QUIT

			network_tyrian_halt(0, true);
		}
	}
	else
	{
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);

		// Show the host's settled handshake settings while details arrive. Waiting
		// for a later confirmation would stall the host in the outpost.
		{
			const char *label[GUEST_WAIT_ROWS_CAP], *value[GUEST_WAIT_ROWS_CAP];
			const int rows = networkGuestWaitRows(label, value);

			// Same column rule as the host menu: the block is as wide as its widest row, centred,
			// with labels on its left edge and values on its right. The floor keeps a screenful of
			// short values from looking cramped without stretching the columns apart.
			int blockW = 140;
			for (int i = 0; i < rows; ++i)
			{
				blockW = MAX(blockW, JE_textWidth(label[i], small_font) + 20
				                     + JE_textWidth(value[i], small_font));
			}
			blockW = MIN(blockW, 300);

			const int xLabel = LEGACY_WIDTH / 2 - blockW / 2;
			const int xValue = xLabel + blockW;

			// Centre the rows, status, and Esc hint below the title.
			const int dyRow = guest_wait_row_h(rows);
			const int blockH = rows * dyRow + guest_wait_gap(rows)
			                   + GUEST_WAIT_LINE_H + GUEST_WAIT_HINT_H;
			const int yTop = GUEST_WAIT_TOP + (GUEST_WAIT_BOTTOM - GUEST_WAIT_TOP - blockH) / 2;

			for (int i = 0; i < rows; ++i)
			{
				const int y = yTop + dyRow * i;
				draw_font_hv_shadow(VGAScreen, xLabel, y, label[i], small_font, left_aligned, 15, 2, false, 1);
				draw_font_hv_shadow(VGAScreen, xValue, y, value[i], small_font, right_aligned, 15, 4, false, 1);
			}

			const int yWait = yTop + rows * dyRow + guest_wait_gap(rows);
			JE_dString(VGAScreen, JE_fontCenter(networkText[4 - 1], SMALL_FONT_SHAPES),
			           yWait, networkText[4 - 1], SMALL_FONT_SHAPES);
			draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, yWait + GUEST_WAIT_LINE_H + 8,
			                    GUEST_WAIT_HINT, small_font, centered, 15, 0, false, 1);
		}

		JE_showVGA();

		// until opponent sends details packet
		newkey = false;  // the press that drove the lobby's join is still latched
		while (true)
		{
			push_joysticks_as_keyboard();
			service_SDL_events(false);

			// Keep the mouse cursor alive while the host picks episode/difficulty.
			mouseCursor = MOUSE_POINTER_NORMAL;
			JE_mouseStart();
			JE_showVGA();
			JE_mouseReplace();
			if (!output_vsync)
				limit_render_fps();

			// Process cancel before a newly arrived start packet.
			if ((newkey && lastkey_scan == SDL_SCANCODE_ESCAPE) || qa_net_guest_esc)
			{
				if (qa_net_guest_esc)
				{
					fprintf(stderr, "net gameplay: joiner escapes the details wait\n");
					fflush(stderr);
				}
				JE_playSampleNum(S_SPRING);
				network_prepare(PACKET_QUIT);
				network_send(4);  // PACKET_QUIT
				network_tyrian_halt(0, true);   // does not return
			}

			// The length matters: packet_copy fills only the first `len` bytes of a reused buffer, so a
			// short packet would set the episode and difficulty from whatever the previous one left
			// behind; a desync before the first tick.
			if (packet_in[0] && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_DETAILS &&
			    packet_in[0]->len >= 10)
				break;

			network_update();
			network_check();
		}

		UDPpacket *const details_packet = packet_in[0];
		if (details_packet == NULL)
		{
			network_tyrian_halt(3, false);
			return;
		}
		const int their_game_type  = SDLNet_Read16(&details_packet->data[4]);
		const int their_episode    = SDLNet_Read16(&details_packet->data[6]);
		const int their_difficulty = SDLNet_Read16(&details_packet->data[8]);

		// The host picked both from its own menus, so out of range means a corrupt packet, not a
		// disagreement.  Worth catching here: JE_initEpisode builds level filenames from the
		// number, and a bad one takes the game down inside the loader instead.
		if (their_game_type != (int)network_game_type ||
		    their_episode < 1 || their_episode > EPISODE_MAX ||
		    their_difficulty < 1 || their_difficulty > DIFFICULTY_10)
		{
			fprintf(stderr, "error: opponent sent unusable game details (%d/%d/%d)\n",
			        their_game_type, their_episode, their_difficulty);
			network_tyrian_halt(3, false);
		}
		network_host_episode = their_episode;
		network_host_difficulty = their_difficulty;

		if (details_packet->len >= 10 + SAVE_RECORD_PACKED_SIZE)
		{
			// Resume form: adopt the host's save record wholesale. Input devices stay local;
			// they name hardware on the host's desk, not simulation state.
			JE_SaveFileType rec;
			save_record_unpack(&rec, &details_packet->data[10]);
			rec.input1 = inputDevice[0];
			rec.input2 = inputDevice[1];

			// Read the tail before the download loop consumes this packet.
			const uint hostSeat = details_packet->len > 10 + SAVE_RECORD_PACKED_SIZE
			                    ? details_packet->data[10 + SAVE_RECORD_PACKED_SIZE] : 0;
			const bool haveCustomTail = details_packet->len >= 19 + SAVE_RECORD_PACKED_SIZE;
			const Uint32 customSize = haveCustomTail
			                        ? SDLNet_Read32(&details_packet->data[11 + SAVE_RECORD_PACKED_SIZE]) : 0;
			const Uint32 customHash = haveCustomTail
			                        ? SDLNet_Read32(&details_packet->data[15 + SAVE_RECORD_PACKED_SIZE]) : 0;

			network_update();

			if (rec.customEpFile[0] != '\0')
			{
				networkCustomSyncScreen();
				SDL_strlcpy(network_host_custom_file, rec.customEpFile,
				            sizeof(network_host_custom_file));
				network_host_custom_size = customSize;
				network_host_custom_hash = customHash;
				if (customSize == 0 || !network_custom_level_fetch())
					network_tyrian_halt(3, false);
			}
			else
			{
				network_host_custom_file[0] = '\0';
				network_host_custom_size = 0;
				network_host_custom_hash = 0;
			}

			gameJustLoaded = true;
			JE_loadGameRecord(&rec, true);
			// A local fallback to the base episode would desync online play.
			if (customEpisodeActive() != (rec.customEpFile[0] != '\0'))
				network_tyrian_halt(3, false);
			coopCampaignMode = network_game_type == NETWORK_GAME_CAMPAIGN;
			coopEndlessMode = network_game_type == NETWORK_GAME_ENDLESS;

			// The host took back the seat it saved in, so this machine flies the other one.
			if (hostSeat == 1 || hostSeat == 2)
			{
				networkHostPlayerNum = hostSeat;
				thisPlayerNum = 3 - networkHostPlayerNum;
			}

			if (coopEndlessMode && network_host_custom_endless != CUSTOM_ENDLESS_OFF)
			{
				networkCustomSyncScreen();
				if (!network_custom_endless_fetch())
					network_tyrian_halt(3, false);
			}

			// Same rule as the host's publish above: no run means no session.
			if (coopEndlessMode && !networkEndlessResume(0))
				network_tyrian_halt(3, false);

			resumed = true;
		}
		else
		{
			network_update();

			if (network_host_custom_file[0] != '\0' &&
			    network_game_type != NETWORK_GAME_ENDLESS && !network_host_timed_battle)
			{
				networkCustomSyncScreen();
				if (!network_custom_level_fetch() || !networkCustomEpisodeActivate())
					network_tyrian_halt(3, false);
			}
			else
			{
				if (network_game_type == NETWORK_GAME_ENDLESS &&
				    network_host_custom_endless != CUSTOM_ENDLESS_OFF)
				{
					networkCustomSyncScreen();
					if (!network_custom_endless_fetch())
						network_tyrian_halt(3, false);
				}
				JE_initEpisode(their_episode);
			}
			initial_episode_num = episodeNum;  // as the host does; see its branch above
			difficultyLevel = their_difficulty;
			initialDifficulty = difficultyLevel - networkDifficultyBump();
		}
		fade_black(10);
	}

	if (!resumed)
	{
		if (coopEndlessMode)
		{
			networkEndlessNewRun();
		}
		else if (coop_mode_active())
		{
			static const Uint32 initial_cash[] = { 10000, 15000, 20000, 30000, 20000 };
			const Uint32 cash = initial_cash[episodeNum - 1];
			player[1].items = player[0].items;
			player[1].last_items = player[0].last_items;
			for (uint i = 0; i < COUNTOF(player); ++i)
			{
				player[i].cash = cash;
				player[i].is_dragonwing = false;
				player[i].lives = &player[i].items.weapon[player_lives_port(i)].power;
			}
		}
		else
		{
			for (uint i = 0; i < COUNTOF(player); ++i)
				player[i].cash = 0;

			if (arcade_separate_mode())
			{
				if (network_game_type == NETWORK_GAME_SUPERTYRIAN)
				{
					// Two SuperTyrian runs side by side: the same Stalker 21.126 and Atomic
					// RailGun newSuperTyrianGame issues, and the twiddle tables key off the
					// superTyrian flag, whose combo state is already per player.
					superTyrian = true;
					for (uint i = 0; i < COUNTOF(player); ++i)
						networkSuperTyrianEquip(&player[i]);
				}
				else if (network_game_type == NETWORK_GAME_SUPERARCADE)
				{
					// Each player chose their own ship on the screen above; the host's is what
					// the session global names, because the save record packs player one's from
					// it (JE_saveGame) and reads it back the same way.
					const int mine = networkSuperArcadeShipSelect();
					const int theirs = network_sa_ship_peer();
					if (mine == 0 || theirs == 0)
						network_tyrian_halt(3, false);   // the pair never met; do not guess a ship
					const uint me = thisPlayerNum >= 2 ? 1u : 0u;
					networkSuperArcadeEquip(&player[me], mine);
					networkSuperArcadeEquip(&player[1 - me], theirs);
					superArcadeMode = (JE_byte)player[0].items.super_arcade_mode;
					fade_black(10);
				}
				else
				{
					// Two personal arcades, each the one-player arcade game: both fly the Stalker
					// off the same fresh arsenal, and each ship's life counter is its own front
					// gun. newGame() gives a solo arcade run exactly this ship.
					player[0].items.ship = 8;  // Stalker
					// Neither ship flies a Super Arcade ruleset here, and the save record's own
					// discriminator is these two bytes, so say so rather than inherit it.
					player[0].items.super_arcade_mode = SA_NONE;
					player[1].items = player[0].items;
				}
				for (uint i = 0; i < COUNTOF(player); ++i)
				{
					player[i].is_dragonwing = false;
					player[i].lives = &player[i].items.weapon[player_lives_port(i)].power;
					player[i].last_items = player[i].items;
				}
			}
			else
			{
				player[0].items.ship = 11;  // Silver Ship
			}
		}

		// Gameplay wire tests: both machines mount the same scripted sidekick combination.
		if (qa_net_gameplay_ticks > 0 && qa_net_loadout > 0)
			qa_net_apply_loadout(qa_net_loadout);
		if (qa_net_gameplay_ticks > 0 && (qa_net_scenario == 5 || qa_net_scenario == 19))
			qa_net_apply_linked_special();
	}
	else if (network_game_type == NETWORK_GAME_CAMPAIGN)
	{
		/* A resumed Campaign jumps straight into its saved level, so it never reaches the initial
		 * outpost window that normally exchanges custom designs. Publish both players' working
		 * copies here before item data materializes the loaded custom port/sidekick placeholders. */
		network_custom_weapon_publish_resume();
	}

	/* Separate Arcade has no outpost. Publish any referenced weapon, then exchange ships. */
	if (custom_ships_multiplayer_mode() && !coop_mode_active())
	{
		customWeaponNetPrepare();
		if (extraShipsUseCustomWeapon())
			network_custom_weapon_publish_resume();
		network_extra_ships_publish();
		network_custom_content_rendezvous();
	}

	while (!network_is_sync())
	{
		service_SDL_events(false);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		if (!output_vsync)
			limit_render_fps();

		network_check();
	}

	// A race is scored from the first tick, so it does not begin until both players say so.
	if (timedBattleMode)
		networkTimedBattleReady();

	if (qa_net_gameplay_ticks > 0)
	{
		// The session flags as this machine armed or adopted them (the doubled-pickups
		// scenario asserts both peers print the same pair), and the soak baseline.
		network_test_mem_mark();
		fprintf(stderr, "net gameplay: details settled, starting the game\n");
		// sa1/sa2 are the two ships' own rulesets, raw (1..SA a Super Arcade hull, 254 SuperTyrian,
		// 0 neither). Both machines equip both ships, so the pair has to read identically here or
		// the picks never really crossed the wire.
		fprintf(stderr, "net session flags: shared=%d doubled=%d separate=%d st=%d sa1=%d sa2=%d\n",
		        coop_credit_is_shared() ? 1 : 0, coop_earnings_are_doubled() ? 1 : 0,
		        arcade_separate_mode() ? 1 : 0, superTyrian ? 1 : 0,
		        (int)player[0].items.super_arcade_mode, (int)player[1].items.super_arcade_mode);
		fflush(stderr);
	}
}
#endif /* WITH_NETWORK */

bool titleScreen(void)
{
	enum MenuItemIndex
	{
		MENU_ITEM_NEW_GAME = 0,
		MENU_ITEM_LOAD_GAME,
		MENU_ITEM_HIGH_SCORES,
		MENU_ITEM_INSTRUCTIONS,
		MENU_ITEM_SETUP,
		MENU_ITEM_EXTRA,
		MENU_ITEM_DEMO,
		MENU_ITEM_QUIT,
	};

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	bool restart = true;

	size_t selectedIndex = MENU_ITEM_NEW_GAME;
	size_t specialNameProgress[SA_ENGAGE] = { 0 };

	// Title menu labels: the 7 data-file entries (menuText[]) with "Extra" inserted
	// after Setup. Keep in sync with enum MenuItemIndex above.
	const char *const titleLabels[] =
	{
		menuText[0],  // Start New Game
		menuText[1],  // Load Game
		menuText[2],  // High Scores
		menuText[3],  // Instructions
		menuText[4],  // Setup
		"Extra",
		menuText[5],  // Demo
		menuText[6],  // Quit
	};

	const int xCenter = LEGACY_WIDTH / 2;
	const int yMenuItems = 98;
	const int hMenuItem = 12;
	int wMenuItem[COUNTOF(titleLabels)] = { 0 };

	for (; ; )
	{
		if (restart)
		{
			// Clear stale input before showing logo controls.
			touch_ui_suppress();
			touch_ui_set_layout(TOUCH_LAYOUT_SKIP);
			play_song(SONG_TITLE);

			JE_loadPic(VGAScreen, 4, false);

			if (*opentyrian_commit)
				draw_font_hv_shadow(VGAScreen, 2, 183, opentyrian_commit, small_font, left_aligned, 15, 0, false, 1);
			draw_font_hv_shadow(VGAScreen, 2, 192, opentyrian_version, small_font, left_aligned, 15, 0, false, 1);

			if (moveTyrianLogoUp)
			{
				memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

				blit_sprite(VGAScreenSeg, 11, 62, PLANET_SHAPES, 146); // tyrian logo
				blit_sprite(VGAScreenSeg, 155, 41, PLANET_SHAPES, 151); // 2000(tm)
				fade_palette(colors, 10, 0, 255 - 16);

				if (smoothMotion)
				{
					// Slide the logo up by real elapsed time, presented at the display
					// rate (the original stepped 2 px per 35Hz tick). yLogo runs 60->4
					// and y2K runs 45->73 over the slide.
					const Uint32 slideStart = SDL_GetTicks();
					const Uint32 slideMs = 800;  // ~matches the original stepped slide

					for (;;)
					{
						touch_ui_set_layout(TOUCH_LAYOUT_SKIP);
						float t = (float)(SDL_GetTicks() - slideStart) / (float)slideMs;
						if (t > 1.0f)
							t = 1.0f;

						const int yLogo = (int)(60.0f - 56.0f * t + 0.5f);
						const int y2K   = (int)(45.0f + 28.0f * t + 0.5f);

						memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);
						blit_sprite(VGAScreenSeg, 11, yLogo, PLANET_SHAPES, 146); // tyrian logo
						blit_sprite(VGAScreenSeg, 155, y2K, PLANET_SHAPES, 151); // 2000(tm)
						JE_showVGA();

						if (JE_anyButton())
							break;
						if (t >= 1.0f)
							break;

						if (!output_vsync)
							limit_render_fps();
					}
				}
				else
				{
					for (int yLogo = 60, y2K = 45; yLogo >= 4; yLogo -= 2, ++y2K)
					{
						touch_ui_set_layout(TOUCH_LAYOUT_SKIP);
						setDelay(2);

						memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);
						blit_sprite(VGAScreenSeg, 11, yLogo, PLANET_SHAPES, 146); // tyrian logo
						blit_sprite(VGAScreenSeg, 155, y2K, PLANET_SHAPES, 151); // 2000(tm)
						JE_showVGA();

						service_wait_delay();
						if (JE_anyButton())
							break;
					}
				}

				// Finish at the normal logo position after a skip.
				memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);
				blit_sprite(VGAScreenSeg, 11, 4, PLANET_SHAPES, 146); // tyrian logo
				blit_sprite(VGAScreenSeg, 155, 73, PLANET_SHAPES, 151); // 2000(tm)
				moveTyrianLogoUp = false;
			}
			else
			{
				blit_sprite(VGAScreenSeg, 11, 4, PLANET_SHAPES, 146); // tyrian logo
				blit_sprite(VGAScreenSeg, 155, 73, PLANET_SHAPES, 151); // 2000(tm)
				fade_palette(colors, 10, 0, 255 - 16);
			}

			// Do not pass skip input to the title menu.
			touch_ui_suppress();
			newkey = false;
			newmouse = false;
			touch_ui_clear_layout();

			// Draw menu items.
			for (size_t i = 0; i < COUNTOF(titleLabels); ++i)
			{
				const char *const text = titleLabels[i];

				wMenuItem[i] = JE_textWidth(text, normal_font);
				const int x = xCenter - wMenuItem[i] / 2;
				const int y = yMenuItems + hMenuItem * i;

				draw_font_hv(VGAScreen, x - 1, y - 1, text, normal_font, left_aligned, 15, -10);
				draw_font_hv(VGAScreen, x + 1, y + 1, text, normal_font, left_aligned, 15, -10);
				draw_font_hv(VGAScreen, x + 1, y - 1, text, normal_font, left_aligned, 15, -10);
				draw_font_hv(VGAScreen, x - 1, y + 1, text, normal_font, left_aligned, 15, -10);
				draw_font_hv(VGAScreen, x,     y,     text, normal_font, left_aligned, 15, -3);
			}

			memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

			mouseCursor = MOUSE_POINTER_NORMAL;

			// Fade in menu items.
			fade_palette(colors, 20, 255 - 16 + 1, 255);

			restart = false;
		}

		memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);

		// Highlight selected menu item.
		draw_font_hv(VGAScreen, xCenter, yMenuItems + hMenuItem * selectedIndex, titleLabels[selectedIndex], normal_font, centered, 15, -1);

		service_SDL_events(true);

		JE_mouseStartFilter(0xF0);
		JE_showVGA();
		JE_mouseReplace();
		if (!output_vsync)
			limit_render_fps();  // pace the cursor redraw to the render-fps cap

		const Uint32 idleStartTick = SDL_GetTicks();

		// Poll finely instead of sleeping 16 ms so the outer loop (and mouse cursor)
		// redraws at the display's refresh rate; a still cursor yields the CPU.
		const Uint16 startMouseX = mouse_x;
		const Uint16 startMouseY = mouse_y;
		bool mouseMoved = false;
		for (;;)
		{
			// Play demo after idle for 30 seconds.
			if (SDL_GetTicks() - idleStartTick > 30000)
			{
				fade_black(15);

				play_demo = true;
				return true;
			}

			push_joysticks_as_keyboard();
			service_SDL_events(false);

			mouseMoved = mouse_x != startMouseX || mouse_y != startMouseY;
			if (newkey || new_text || newmouse || mouseMoved)
				break;

			SDL_Delay(1);  // brief idle poll; a still cursor doesn't need redrawing
		}

		// Handle interaction.

		bool action = false;
		bool done = false;

		if (mouseMoved || newmouse)
		{
			// Find menu item that was hovered or clicked.
			for (size_t i = 0; i < COUNTOF(titleLabels); ++i)
			{
				const int xMenuItem = xCenter - wMenuItem[i] / 2;
				if (mouse_x >= xMenuItem && mouse_x < xMenuItem + wMenuItem[i])
				{
					const int yMenuItem = yMenuItems + hMenuItem * i;
					if (mouse_y >= yMenuItem && mouse_y < yMenuItem + hMenuItem)
					{
						if (selectedIndex != i)
						{
							JE_playSampleNum(S_CURSOR);

							selectedIndex = i;
						}

						if (newmouse && lastmouse_but == SDL_BUTTON_LEFT &&
						    lastmouse_x >= xMenuItem && lastmouse_x < xMenuItem + wMenuItem[i] &&
						    lastmouse_y >= yMenuItem && lastmouse_y < yMenuItem + hMenuItem)
						{
							action = true;
						}

						break;
					}
				}
			}
		}

		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);

				done = true;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == 0
					? COUNTOF(titleLabels) - 1
					: selectedIndex - 1;
				break;
			}
			case SDL_SCANCODE_DOWN:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == COUNTOF(titleLabels) - 1
					? 0
					: selectedIndex + 1;
				break;
			}
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
			{
				action = true;
				break;
			}
			case SDL_SCANCODE_ESCAPE:
			{
				JE_playSampleNum(S_SPRING);

				done = true;
			}
			default:
				break;
			}
		}

		if (new_text)
		{
			for (size_t ti = 0U; last_text[ti] != '\0'; ++ti)
			{
				const char c = toupper(last_text[ti]);

				for (size_t i = 0; i < SA_ENGAGE; i++)
				{
					if (specialNameProgress[i] >= COUNTOF(specialName[i]) - 1 ||
						c != specialName[i][specialNameProgress[i]])
					{
						specialNameProgress[i] = 0;
						continue;
					}

					specialNameProgress[i]++;

					if (specialName[i][specialNameProgress[i]] == '\0')
					{
						if (i + 1 == SA_DESTRUCT)
						{
							fade_black(10);

							loadDestruct = true;
							return true;
						}
						else if (i + 1 == SA_ENGAGE)
						{
							JE_playSampleNum(V_DANGER);

							JE_whoa();
							set_colors((SDL_Color) { 0, 0, 0 }, 0, 255);

							if (newSuperTyrianGame())
								return true;

							restart = true;
						}
						else
						{
							fade_black(10);

							if (newSuperArcadeGame(i))
								return true;

							restart = true;
						}
					}
				}
			}
		}

		if (action)
		{
			JE_playSampleNum(S_SELECT);

			switch (selectedIndex)
			{
			case MENU_ITEM_NEW_GAME:
			{
				fade_black(15);

				if (newGame())
					return true;

				restart = true;
				break;
			}
			case MENU_ITEM_LOAD_GAME:
			{
				fade_black(15);

				if (JE_loadScreen(false, false) > 0)
					return true;

				restart = true;
				break;
			}
			case MENU_ITEM_HIGH_SCORES:
			{
				fade_black(15);

				JE_highScoreScreen();

				restart = true;
				break;
			}
			case MENU_ITEM_INSTRUCTIONS:
			{
				fade_black(15);

				JE_helpSystem(1);

				restart = true;
				break;
			}
			case MENU_ITEM_SETUP:
			{
				fade_black(15);

				setupMenu();

				restart = true;
				break;
			}
			case MENU_ITEM_EXTRA:
			{
				fade_black(15);

				if (extraMenu())  // launched a game (SuperTyrian / Super Arcade)?
					return true;

				restart = true;
				break;
			}
			case MENU_ITEM_DEMO:
			{
				fade_black(15);

				play_demo = true;
				return true;
			}
			case MENU_ITEM_QUIT:
			{
				fade_black(15);

				return false;
			}
			default:
				break;
			}
		}

		if (done)
		{
			fade_black(15);

			return false;
		}
	}
}

bool newGame(void)
{
	if (gameplaySelect())
	{
#ifdef WITH_NETWORK
		// The multiplayer lobby connected us from inside the mode menu. networkStartScreen() runs the
		// episode/difficulty handshake; the host picks and sends, the joiner waits and receives, then
		// initializes both ships and purses.
		if (isNetworkGame)
		{
			networkStartScreen();

			gameLoaded = true;
			return true;
		}
#endif

		// Endless was picked in the mode menu: newEndlessGame does its own difficulty select
		// and full setup (episode 1, starting cash, flags) and resets endlessMode on cancel.
		if (endlessMode)
			return newEndlessGame();

		if (timedBattleMode)
		{
			onePlayerAction = true;
			if (timedBattleSelect() && difficultySelect())
				gameLoaded = true;
		}
		else if (episodeSelect() && difficultySelect())
			gameLoaded = true;

		initialDifficulty = difficultyLevel;

		// Only a confirmed start writes the loadout. titleScreen() loops on its own and
		// JE_initPlayerData runs a level above it, so a write made while backing out of the
		// selects would carry into the next mode picked in the same visit.
		if (gameLoaded)
		{
			if (onePlayerAction)
			{
				player[0].cash = 0;

				player[0].items.ship = 8;  // Stalker
			}
			else if (twoPlayerMode)
			{
				for (uint i = 0; i < COUNTOF(player); ++i)
					player[i].cash = 0;

				player[0].items.ship = 11;  // Silver Ship

				difficultyLevel++;

				inputDevice[0] = 1;
				inputDevice[1] = 2;
			}
			else if (richMode)
			{
				player[0].cash = 1000000;
			}
			else
			{
				// keeps whatever ship the session already carries

				const ulong initial_cash[] = { 10000, 15000, 20000, 30000, 20000 };

				assert(episodeNum >= 1 && episodeNum <= EPISODE_AVAILABLE);
				player[0].cash = initial_cash[episodeNum - 1];
			}
		}
	}

	return gameLoaded;
}

bool newSuperArcadeGame(unsigned int i)
{
	if (episodeSelect() && difficultySelect())
	{
		// Claimed only once the picks are confirmed, so backing out leaves the ship alone.
		player[0].items.ship = SAShip[i];

		/* Start special mode! */
		JE_loadPic(VGAScreen, 1, false);
		JE_clr256(VGAScreen);
		JE_dString(VGAScreen, JE_fontCenter(superShips[0], FONT_SHAPES), 30, superShips[0], FONT_SHAPES);
		JE_dString(VGAScreen, JE_fontCenter(superShips[i + 1], SMALL_FONT_SHAPES), 100, superShips[i + 1], SMALL_FONT_SHAPES);
		tempW = ships[player[0].items.ship].shipgraphic;
		if (tempW > 500)
			blit_sprite2x2(VGAScreen, 148, 70, spriteSheetT2000, tempW - 500);
		else if (tempW == 1)
		{
			// Nort Ship: shipgraphic 1 is a sentinel (see JE_playerMovement / JE_drawItem), so draw its
			// two-piece hull here rather than treating 1 as a sprite index.
			blit_sprite2x2(VGAScreen, 148 - 12, 70, spriteSheet9, 220);
			blit_sprite2x2(VGAScreen, 148 + 12, 70, spriteSheet9, 222);
		}
		else
			blit_sprite2x2(VGAScreen, 148, 70, spriteSheet9, tempW);

		JE_showVGA();
		fade_palette(colors, 50, 0, 255);

		wait_input(true, true, true);

		twoPlayerMode = false;
		onePlayerAction = true;
		superArcadeMode = i + 1;
		timedBattleMode = false;
		gameLoaded = true;
		initialDifficulty = ++difficultyLevel;

		player[0].cash = 0;

		// Online Super Arcade stores each player's ship for per-ship weapon lookup.
		player[0].items.super_arcade_mode = (Uint8)(i + 1);
		player[0].items.weapon[FRONT_WEAPON].id = SAWeapon[i][0];
		player[0].items.special = SASpecialWeapon[i];
		if (superArcadeMode == SA_NORTSHIPZ)
		{
			for (uint i = 0; i < COUNTOF(player[0].items.sidekick); ++i)
				player[0].items.sidekick[i] = 24;  // Companion Ship Quicksilver
		}

		fade_black(10);
	}

	return gameLoaded;
}

bool newSuperTyrianGame(void)
{
	/* SuperTyrian */

	initialDifficulty = keysactive[SDL_SCANCODE_SCROLLLOCK] ? DIFFICULTY_SUICIDE : DIFFICULTY_LORD_OF_GAME;

	JE_clr256(VGAScreen);
	JE_outText(VGAScreen, 10, 10, superTyrianText[0], 15, 4);
	if (initialDifficulty == DIFFICULTY_LORD_OF_GAME)
		JE_outText(VGAScreen, 10, 20, superTyrianText[1], 15, 4);
	else
		JE_outText(VGAScreen, 10, 20, superTyrianText[2], 15, 4);
	JE_outText(VGAScreen, 10, 30, superTyrianText[3], 15, 4);
	if (initialDifficulty == DIFFICULTY_LORD_OF_GAME)
		JE_outText(VGAScreen, 10, 40, superTyrianText[4], 15, 4);
	JE_outText(VGAScreen, 10, 60, superTyrianText[5], 15, 4);

	char buf[10 + 1 + 15 + 1];
	snprintf(buf, sizeof(buf), "%s %s", miscTextB[4], pName[0]);
	JE_dString(VGAScreen, JE_fontCenter(buf, FONT_SHAPES), 110, buf, FONT_SHAPES);

	play_song(16);
	JE_playSampleNum(V_GOOD_LUCK);

	JE_showVGA();
	fade_palette(colors, 10, 0, 255);

	wait_noinput(true, true, true);
	wait_input(true, true, true);

	fade_black(1);
	if (episodeSelect()) // T2000 let you choose the starting episode
	{
		constantDie = false;
		superTyrian = true;
		onePlayerAction = true;
		timedBattleMode = false;
		gameLoaded = true;
		difficultyLevel = initialDifficulty;

		player[0].cash = 0;

		player[0].items.ship = 13;                     // The Stalker 21.126
		player[0].items.weapon[FRONT_WEAPON].id = 39;  // Atomic RailGun

		fade_black(10);
		return true;
	}
	else
	{
		play_song(SONG_TITLE);
		return false;
	}

}

bool newEndlessGame(void)
{
	/* Endless roguelite mode. */

	// Absorb the menu-selection keypress/click so it doesn't fall straight through
	// into the difficulty picker (which would auto-select the default and skip it).
	wait_noinput(true, true, true);

	// Endless always starts at episode 1; the run traverses episodes as it deepens.
	JE_initEpisode(1);
	initial_episode_num = episodeNum;

	// Choose the run seed (random or typed), the run mode and the base-level rule before the
	// difficulty picker. Cancelling here backs all the way out to the title, exactly like
	// cancelling difficulty.
	char seedbuf[ENDLESS_SEED_MAXLEN];
	EndlessRunMode runMode = ENDLESS_RUNMODE_STANDARD;
	EndlessBaseRule baseRule = ENDLESS_BASE_VARIED;
	if (!endlessSeedSelect(seedbuf, sizeof(seedbuf), &runMode, &baseRule))
	{
		endlessMode = false;
		play_song(SONG_TITLE);
		return false;
	}

	if (!difficultySelect())
	{
		endlessMode = false;  // cancelled: don't leave the mode flag set for the next new game
		play_song(SONG_TITLE);
		return false;
	}
	initialDifficulty = difficultyLevel;

	endlessResetRun();
	endlessSetSeed(seedbuf);  // establish the run's seeded structural RNG (endlessResetRun blanked it)
	endlessRunMode = runMode;  // apply the seed screen's mode choice (endlessResetRun reset it)
	endlessRunBaseRule = baseRule;   // ...and its chart rule, pinned for the whole run
	endlessRecordRunStart();  // baseline the all-time record so this run's "(+n)" measures only what IT gained

	endlessMode = true;
	onePlayerAction = false;  // full game: cash economy + between-level shops, NOT arcade orb drops
	timedBattleMode = false;
	twoPlayerMode = false;
	superTyrian = false;
	superArcadeMode = SA_NONE;
	gameLoaded = true;
	difficultyLevel = initialDifficulty;

	// Difficulty-based starting cash for the first shop. Emptied and then credited THROUGH the ledger
	// rather than assigned: the stake is the run's first income, and anything the previous game left
	// in the wallet is not this run's money. Keeps earned - spent == wallet true from zone 1.
	player[0].cash = 0;
	endlessCashResync();
	endlessCashCredit(endlessStartingCash(), ENDLESS_CASH_START);

	endlessApplyStartingLoadout();  // Atomic RailGun front gun (the depth-0 outpost re-applies it too)

	fade_black(10);
	return true;
}

void intro_logos(void)
{
	moveTyrianLogoUp = true;
	touch_ui_suppress();

	SDL_FillRect(VGAScreen, NULL, 0);

	fade_white(25);

	JE_loadPic(VGAScreen, 10, false);
	touch_ui_set_layout(TOUCH_LAYOUT_SKIP);
	JE_showVGA();

	fade_palette(colors, 25, 0, 255);

	setDelay(200);
	wait_delayorinput();

	fade_black(10);

	JE_loadPic(VGAScreen, 12, false);
	touch_ui_set_layout(TOUCH_LAYOUT_SKIP);
	JE_showVGA();

	fade_palette(colors, 10, 0, 255);

	setDelay(200);
	wait_delayorinput();

	fade_black(10);
}

void JE_readTextSync(void)
{
#if 0  // this function seems to be unnecessary
	JE_clr256(VGAScreen);
	JE_showVGA();
	JE_loadPic(VGAScreen, 1, true);

	JE_barShade(VGAScreen, 3, 3, 316, 196);
	JE_barShade(VGAScreen, 1, 1, 318, 198);
	JE_dString(VGAScreen, 10, 160, "Waiting for other player.", SMALL_FONT_SHAPES);
	JE_showVGA();

	do
	{
		setjasondelay(2);

		wait_delay();

	} while (0);
#endif
}

void JE_displayText(void)
{
	/* Display Warning Text */
	JE_word tempY = 55;
	if (warningRed)
	{
		tempY = 2;
	}
	for (temp = 0; temp < levelWarningLines; temp++)
	{
		if (!ESCPressed)
		{
			JE_outCharGlow(10, tempY, levelWarningText[temp]);

			if (haltGame)
			{
				JE_tyrianHalt(5);
			}

			tempY += 10;
		}
	}
	if (frameCountMax != 0)
	{
		frameCountMax = 6;
		temp = 1;
	}
	else
	{
		temp = 0;
	}
	textGlowFont = TINY_FONT;
	tempW = 184;
	if (warningRed)
		tempW = 7 * 16 + 6;

	JE_outCharGlow(JE_fontCenter(miscText[4], TINY_FONT), tempW, miscText[4]);

	do
	{
		// A gameplay wire test has no player to press past the briefing.
		if (qa_net_gameplay_ticks > 0)
			break;

		touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);
		touch_ui_idle_repaint();
		if (levelWarningDisplay)
			JE_updateWarning(VGAScreen);

		setDelay(1);

		NETWORK_KEEP_ALIVE();

		wait_delay();

	} while (!(JE_anyButton() || (frameCountMax == 0 && temp == 1) || ESCPressed));
	levelWarningDisplay = false;
}

Sint16 JE_newEnemy(int enemyOffset, Uint16 eDatI, Sint16 uniqueShapeTableI)
{
	for (int i = enemyOffset; i < enemyOffset + 25; ++i)
	{
		if (enemyAvail[i] == 1)
		{
			enemyAvail[i] = JE_makeEnemy(&enemy[i], eDatI, uniqueShapeTableI);
			enemy_armed_flash_clear(i);
			return i + 1;
		}
	}
	
	return 0;
}

/* Arcade weapon-ball randomizer.
 * Reroll each weapon pickup within its class using pools derived from the current episode's
 * enemyDat. Purple and +1 power balls are unchanged. */
enum
{
	BALL_CLASS_FRONT,     // value 30001..30999 -> front weapon
	BALL_CLASS_REAR,      // value 31000..31999 -> rear weapon
	BALL_CLASS_SIDEKICK,  // value 32000..32099 -> sidekick
	BALL_CLASS_SPECIAL,   // value 32100+       -> special weapon
	BALL_CLASS_COUNT
};

#define BALL_POOL_MAX 32

static Uint16 ballPool[BALL_CLASS_COUNT][BALL_POOL_MAX];
static int ballPoolLen[BALL_CLASS_COUNT];

// Ball enemies sit on the Power-ups sprite sheet (shape bank 26) and encode what they grant
// in `value`; JE_playerCollide (mainint.c) decodes the very same ranges on pickup.
static int arcadeBallClass(Uint16 eDatI)
{
	if (eDatI > ENEMY_NUM || enemyDat[eDatI].shapebank != 26)
		return -1;

	const Sint16 value = enemyDat[eDatI].value;
	if (value <= 30000)  // 30000 is the purple ball; nothing below it on this sheet grants a weapon
		return -1;
	if (value < 31000)
		return BALL_CLASS_FRONT;
	if (value < 32000)
		return BALL_CLASS_REAR;
	if (value < 32100)
		return BALL_CLASS_SIDEKICK;
	return BALL_CLASS_SPECIAL;
}

// Rebuilt per level, after the episode's item data is in place (see the level init below).
void JE_buildArcadeBallPools(void)
{
	memset(ballPoolLen, 0, sizeof(ballPoolLen));

	for (Uint16 i = 0; i <= ENEMY_NUM; ++i)
	{
		const int cls = arcadeBallClass(i);
		if (cls < 0 || ballPoolLen[cls] >= BALL_POOL_MAX)
			continue;
		ballPool[cls][ballPoolLen[cls]++] = i;
	}
}

// Applies to Arcade and Super Arcade, excluding scripted SuperTyrian/Galaga loadouts and Endless.
static bool arcadeRandomPickupsOn(void)
{
	return arcadeRandomBalls
	    && arcade_rules_active()
	    && !superTyrian
	    && !galagaMode
	    && !timedBattleMode
	    && !endlessMode;
}

// Super Arcade rerolls its five-color weapon index after spawn, not the ball id here.
static bool arcadeBallRandomActive(void)
{
	return arcadeRandomPickupsOn() && superArcadeMode == SA_NONE;
}

// A Super Arcade color index selects one of the current ship's five weapons.
static bool arcadeSuperPickupRandomActive(void)
{
	return arcadeRandomPickupsOn() && superArcadeMode != SA_NONE;
}

// One simulation RNG draw per ball; the host also synchronizes the toggle.
static Uint16 arcadeRandomizeBall(Uint16 eDatI)
{
	const int cls = arcadeBallClass(eDatI);
	if (cls < 0 || ballPoolLen[cls] == 0)
		return eDatI;

	return ballPool[cls][mt_rand() % ballPoolLen[cls]];
}

uint JE_makeEnemy(struct JE_SingleEnemyType *enemy, Uint16 eDatI, Sint16 uniqueShapeTableI)
{
	uint avail;

	JE_byte shapeTableI;

	if (superArcadeMode != SA_NONE && eDatI == 534)
		eDatI = 533;

	// Reroll at creation so every scripted and death-drop path is covered.
	if (arcadeBallRandomActive())
		eDatI = arcadeRandomizeBall(eDatI);

	// Endless: a weapon powerup whose gun is already maxed becomes the other gun's powerup, or the
	// 5000 gem when both are full. Here rather than at the drop site so every spawn path is covered.
	if (endlessMode)
		eDatI = endlessResolvePowerupDrop(eDatI);

	if (uniqueShapeTableI > 0)
	{
		shapeTableI = uniqueShapeTableI;
	}
	else
	{
		shapeTableI = enemyDat[eDatI].shapebank;
	}

	Sprite2_array *sprite2s = NULL;
	if (shapeTableI == 21)
	{
		sprite2s = &spriteSheet11;  // Coins&Gems
	}
	else if (shapeTableI == 26)
	{
		sprite2s = &spriteSheet10;  // Two-Player Stuff
	}
	else
	{
		for (size_t i = 0; i < COUNTOF(enemySpriteSheetIds); ++i)
			if (shapeTableI == enemySpriteSheetIds[i])
				sprite2s = &enemySpriteSheets[i];
	}
	
	if (sprite2s != NULL)
		enemy->sprite2s = sprite2s;
	else
		// Use shape table value from previous enemy that occupied the enemy slot. (Ex. APPROACH.)
		fprintf(stderr, "warning: ignoring sprite from unloaded shape table %d\n", shapeTableI);

	enemy->enemydatofs = &enemyDat[eDatI];

	enemy->mapoffset = 0;
	enemy->mapoffset_frac = 0.0f;
	enemy->scroll_ybase = 0;
	enemy->scroll_yfrac = 0.0f;
	enemy->scroll_ylayer = 0;

	for (uint i = 0; i < 3; ++i)
	{
		enemy->eshotmultipos[i] = 0;
		enemy->eshotextracredit[i] = 0;
	}

	enemy->enemyground = (enemyDat[eDatI].explosiontype & 1) == 0;
	enemy->explonum = enemyDat[eDatI].explosiontype >> 1;

	enemy->launchfreq = enemyDat[eDatI].elaunchfreq;
	enemy->launchwait = enemyDat[eDatI].elaunchfreq;

	// T2000 ... Account for the second enemy bank only if we're creating something from it
	if (eDatI > 1000)
	{
		enemy->launchtype = enemyDat[eDatI].elaunchtype;
		enemy->launchspecial = 0;
	}
	else
	{
		enemy->launchtype = enemyDat[eDatI].elaunchtype % 1000;
		enemy->launchspecial = enemyDat[eDatI].elaunchtype / 1000;
	}

	// Dispenser restore: the dormant 2x2 base (pieces 80-83) gets its cousin hatch's cadence so
	// the shipped 17-frame open/close cycle finally plays; same-tick creation keeps all four
	// quadrants in sync.
	if (dispenserBasesActive && eDatI >= 80 && eDatI <= 83)
	{
		enemy->launchfreq = 40;
		enemy->launchwait = 40;
		enemy->launchspecial = 0;
	}

	enemy->xaccel = enemyDat[eDatI].xaccel;
	enemy->yaccel = enemyDat[eDatI].yaccel;

	// Homing tiers floor weak tracking at 90, 92, or 96. Stronger tracking survives;
	// Rampage also adds collision damage. Scenery is restored on its first processed frame.
	if (endlessFxActive())
	{
		const int trackFloor = (endlessActiveMods & ENDLESS_MOD_RAMPAGE)  ? 96
		                     : (endlessActiveMods & ENDLESS_MOD_KAMIKAZE) ? 92
		                     : (endlessActiveMods & ENDLESS_MOD_HOMING)   ? 90
		                     : 0;
		if (trackFloor)
		{
			if (enemy->xaccel < trackFloor) enemy->xaccel = trackFloor;
			if (enemy->yaccel < trackFloor) enemy->yaccel = trackFloor;
		}
	}

	enemy->xminbounce = -10000;
	enemy->xmaxbounce = 10000;
	enemy->yminbounce = -10000;
	enemy->ymaxbounce = 10000;
	/*Far enough away to be impossible to reach*/

	for (uint i = 0; i < 3; ++i)
	{
		enemy->tur[i] = enemyDat[eDatI].tur[i];
	}

	enemy->ani = enemyDat[eDatI].ani;
	enemy->animin = 1;

	switch (enemyDat[eDatI].animate)
	{
	case 0:
		enemy->enemycycle = 1;
		enemy->aniactive = 0;
		enemy->animax = 0;
		enemy->aniwhenfire = 0;
		break;
	case 1:
		enemy->enemycycle = 0;
		enemy->aniactive = 1;
		enemy->animax = 0;
		enemy->aniwhenfire = 0;
		break;
	case 2:
		enemy->enemycycle = 1;
		enemy->aniactive = 2;
		enemy->animax = enemy->ani;
		enemy->aniwhenfire = 2;
		break;
	}

	if (enemyDat[eDatI].startxc != 0)
		enemy->ex = enemyDat[eDatI].startx + (mt_rand() % (enemyDat[eDatI].startxc * 2)) - enemyDat[eDatI].startxc + 1;
	else
		enemy->ex = enemyDat[eDatI].startx + 1;

	if (enemyDat[eDatI].startyc != 0)
		enemy->ey = enemyDat[eDatI].starty + (mt_rand() % (enemyDat[eDatI].startyc * 2)) - enemyDat[eDatI].startyc + 1;
	else
		enemy->ey = enemyDat[eDatI].starty + 1;

	enemy->exc = enemyDat[eDatI].xmove;
	enemy->eyc = enemyDat[eDatI].ymove;
	enemy->excc = enemyDat[eDatI].xcaccel;
	enemy->eycc = enemyDat[eDatI].ycaccel;
	enemy->exccw = abs(enemy->excc);
	enemy->exccwmax = enemy->exccw;
	enemy->eyccw = abs(enemy->eycc);
	enemy->eyccwmax = enemy->eyccw;
	enemy->exccadd = (enemy->excc > 0) ? 1 : -1;
	enemy->eyccadd = (enemy->eycc > 0) ? 1 : -1;
	enemy->special = false;
	enemy->iced = 0;

	if (enemyDat[eDatI].xrev == 0)
		enemy->exrev = 100;
	else if (enemyDat[eDatI].xrev == -99)
		enemy->exrev = 0;
	else
		enemy->exrev = enemyDat[eDatI].xrev;

	if (enemyDat[eDatI].yrev == 0)
		enemy->eyrev = 100;
	else if (enemyDat[eDatI].yrev == -99)
		enemy->eyrev = 0;
	else
		enemy->eyrev = enemyDat[eDatI].yrev;

	enemy->exca = (enemy->xaccel > 0) ? 1 : -1;
	enemy->eyca = (enemy->yaccel > 0) ? 1 : -1;

	enemy->enemytype = eDatI;

	for (uint i = 0; i < 3; ++i)
	{
		if (enemy->tur[i] == 252)
			enemy->eshotwait[i] = 1;
		else if (enemy->tur[i] > 0)
			enemy->eshotwait[i] = 20;
		else
			enemy->eshotwait[i] = 255;
	}
	for (uint i = 0; i < 20; ++i)
		enemy->egr[i] = enemyDat[eDatI].egraphic[i];
	enemy->size = enemyDat[eDatI].esize;
	enemy->linknum = 0;
	enemy->edamaged = enemyDat[eDatI].dani < 0;
	enemy->enemydie = enemyDat[eDatI].eenemydie;

	enemy->freq[1-1] = enemyDat[eDatI].freq[1-1];
	enemy->freq[2-1] = enemyDat[eDatI].freq[2-1];
	enemy->freq[3-1] = enemyDat[eDatI].freq[3-1];

	enemy->edani   = enemyDat[eDatI].dani;
	enemy->edgr    = enemyDat[eDatI].dgr;
	enemy->edlevel = enemyDat[eDatI].dlevel;

	enemy->fixedmovey = 0;
	enemy->fixedmovey_carry = 0;
	enemy->fixedmovey_carry_base = 0;
	enemy->fixedmovey_carry_move = 0;

	enemy->filter = 0x00;

	int tempValue = 0;
	if (enemyDat[eDatI].value > 1 && enemyDat[eDatI].value < 10000)
	{
		switch (difficultyLevel)
		{
		case -1:
		case DIFFICULTY_WIMP:
			tempValue = enemyDat[eDatI].value * 0.75f;
			break;
		case DIFFICULTY_EASY:
		case DIFFICULTY_NORMAL:
			tempValue = enemyDat[eDatI].value;
			break;
		case DIFFICULTY_HARD:
			tempValue = enemyDat[eDatI].value * 1.125f;
			break;
		case DIFFICULTY_IMPOSSIBLE:
			tempValue = enemyDat[eDatI].value * 1.5f;
			break;
		case DIFFICULTY_INSANITY:
			tempValue = enemyDat[eDatI].value * 2;
			break;
		case DIFFICULTY_SUICIDE:
			tempValue = enemyDat[eDatI].value * 2.5f;
			break;
		case DIFFICULTY_MANIACAL:
		case DIFFICULTY_LORD_OF_GAME:
			tempValue = enemyDat[eDatI].value * 4;
			break;
		case DIFFICULTY_NORTANEOUS:
		case DIFFICULTY_10:
			tempValue = enemyDat[eDatI].value * 8;
			break;
		}
		if (expertMode)  // expert-mode cash bonus to offset the harsher economy
			tempValue = tempValue * expertScorePct / 100;
		if (tempValue > 10000)
			tempValue = 10000;
		enemy->evalue = tempValue;
	}
	else
	{
		enemy->evalue = enemyDat[eDatI].value;
	}

	int tempArmor = 1;
	if (enemyDat[eDatI].armor > 0)
	{
		if (enemyDat[eDatI].armor != 255)
		{
			switch (difficultyLevel)
			{
			case -1:
			case DIFFICULTY_WIMP:
				tempArmor = enemyDat[eDatI].armor * 0.5f + 1;
				break;
			case DIFFICULTY_EASY:
				tempArmor = enemyDat[eDatI].armor * 0.75f + 1;
				break;
			case DIFFICULTY_NORMAL:
				tempArmor = enemyDat[eDatI].armor;
				break;
			case DIFFICULTY_HARD:
				tempArmor = enemyDat[eDatI].armor * 1.2f;
				break;
			case DIFFICULTY_IMPOSSIBLE:
				tempArmor = enemyDat[eDatI].armor * 1.5f;
				break;
			case DIFFICULTY_INSANITY:
				tempArmor = enemyDat[eDatI].armor * 1.8f;
				break;
			case DIFFICULTY_SUICIDE:
				tempArmor = enemyDat[eDatI].armor * 2;
				break;
			case DIFFICULTY_MANIACAL:
				tempArmor = enemyDat[eDatI].armor * 3;
				break;
			case DIFFICULTY_LORD_OF_GAME:
				tempArmor = enemyDat[eDatI].armor * 4;
				break;
			case DIFFICULTY_NORTANEOUS:
			case DIFFICULTY_10:
				tempArmor = enemyDat[eDatI].armor * 8;
				break;
			}

			if (endlessFxActive())
				tempArmor = tempArmor * endlessArmorPercent() / 100;

			// Expert mode toughens every enemy; bosses sit near the 254 cap already
			// and get their extra HP from expertBossHpMult instead.
			if (expertMode)
				tempArmor = tempArmor * expertEnemyArmorPct / 100;

			if (tempArmor > 254)
			{
				tempArmor = 254;
			}
		}
		else
		{
			tempArmor = 255;
		}

		enemy->armorleft = tempArmor;

		avail = 0;
		enemy->scoreitem = false;
	}
	else
	{
		avail = 2;
		enemy->armorleft = 255;
		if (enemy->evalue != 0)
			enemy->scoreitem = true;
	}

	enemy->damageAccum = 0;  // reset the scaled-HP damage accumulator on (re)spawn
	enemy->chainWave = 0;    // a wave still in the air must not skip the slot's new occupant
	enemy->healthbar_seen = false;  // no enemy HP bar until this slot takes damage
	enemy->healthbar_max = 0;       // an invincible spawn has no health value and keeps this 0
	enemy_note_full_armor(enemy);   // any other spawn takes its scaled armor as full
	enemy->eliteState = 0;  // endless: elite undecided until first processed (see JE_drawEnemy)
	enemy->groupHomed = false;
	// Only an enemy that can actually track draws for a side, so the roll costs the shared RNG
	// stream nothing on the score pickups and straight-line traffic that make up most of a zone.
	enemy->homeTarget = (enemy->xaccel || enemy->yaccel) ? (JE_byte)endlessRollHomingTarget() : 0;

	if (!enemy->scoreitem)
	{
		totalEnemy++;  /*Destruction ratio*/
	}

	/* indicates what to set ENEMYAVAIL to */
	return avail;
}

// Signed round-to-nearest division. Spawn catch-up values are small, but keeping the negative
// path symmetric matters for fixedmovey/eyc combinations whose net motion is upward.
static int event_scroll_round_div(int numerator, int denominator)
{
	if (denominator <= 0)
		return 0;
	return numerator >= 0
	       ? (numerator + denominator / 2) / denominator
	       : -((-numerator + denominator / 2) / denominator);
}

// Catch a new layer-bound enemy up through the remainder of the previous scroll
// tick. Use actual layer motion so fractional carry and fixed movement stay correct.
static int event_enemy_scroll_catchup(JE_word enemyOffset, const struct JE_SingleEnemyType *e)
{
	int layer = 0;
	if (enemyOffset == 25 || enemyOffset == 75)
		layer = 1;
	else if (enemyOffset == 50)
		layer = 3;
	else if (enemyOffset == 0 && enemy_rides_layer2(e))
		layer = 2;  // attached sky scenery rides layer 2 through eyc and/or fixedmovey (see JE_drawEnemy)
	else
		return 0;  // free-flying sky enemies are not vertically layer-bound

	const int eventTime = (int)eventRec[eventLoc - 1].eventtime;
	const int span = eventScrollTo - eventScrollFrom;
	if (!eventScrollCatchupValid || (int)curLoc != eventScrollTo || span <= 0 ||
	    eventTime <= eventScrollFrom || eventTime > eventScrollTo)
		return 0;  // exact-time event, level/event jump, or a non-scroll forceEvents interval

	const int late = eventScrollTo - eventTime;

	if (layer == 2)
	{
		// Anchor sky spawns to one ideal layer/event phase to avoid one-pixel seams.
		if (!eventScrollSkyValid || late < 0)
			return 0;
		int catchup = event_scroll_round_div(eventScrollSkyRatio100 * late +
		                                     eventScrollSkyPhase100, 100);
		const int surplus = ((int)e->fixedmovey + (e->eycc != 0 ? 0 : (int)e->eyc)) -
		                    (int)backMove2;
		if (surplus != 0)
			catchup += event_scroll_round_div(surplus * late, span);
		return catchup;
	}

	if (late <= 0)
		return 0;

	const int fixedMoveRaw = e->fixedmovey;
	const int scalable = enemy_scalable_fixed_y(fixedMoveRaw, e->eyc);
	int fixedMoveScaled = scalable;
	const int baseStep = eventScrollBaseStep[layer];
	if (scalable != 0 && eventScrollBoost > 0 && baseStep > 0)
	{
		if (eventScrollDelayMax[layer] == 1)
			fixedMoveScaled = scalable * eventScrollLayerDelta[layer] / baseStep;
		else
			fixedMoveScaled = scalable * (100 + eventScrollBoost) / 100;
	}
	const int fixedMove = (fixedMoveRaw - scalable) + fixedMoveScaled;

	// Apply the fraction of the previous tick after this event coordinate.
	const int fullMove = eventScrollLayerDelta[layer] + fixedMove + e->eyc;
	return event_scroll_round_div(fullMove * late, span);
}

void JE_createNewEventEnemy(JE_byte enemyTypeOfs, JE_word enemyOffset, Sint16 uniqueShapeTableI)
{
	int i;

	b = 0;

	for (i = enemyOffset; i < enemyOffset + 25; i++)
	{
		if (enemyAvail[i] == 1)
		{
			b = i + 1;
			break;
		}
	}

	if (b == 0)
		return;

	tempW = eventRec[eventLoc-1].eventdat + enemyTypeOfs;

	enemyAvail[b-1] = JE_makeEnemy(&enemy[b-1], tempW, uniqueShapeTableI);
	enemy_armed_flash_clear(b-1);

	// When T2000 gives an X position of -200, what it actually wants is a random X position...
	if (eventRec[eventLoc-1].eventdat2 == -200)
	{
		// Ranged 24 - 231
		eventRec[eventLoc-1].eventdat2 = (mt_rand() % 208) + 24;
	}

	if (eventRec[eventLoc-1].eventdat2 != -99)
	{
		switch (enemyOffset)
		{
		case 0:
			enemy[b - 1].ex = eventRec[eventLoc - 1].eventdat2 - (mapX - 3) * 24;
			enemy[b - 1].ey -= backMove2;
			break;
		case 25:
		case 75:
			enemy[b - 1].ex = eventRec[eventLoc - 1].eventdat2 - (mapX - 3) * 24 - 12;
			enemy[b - 1].ey -= backMove;
			break;
		case 50:
			if (background3x1)
				enemy[b - 1].ex = eventRec[eventLoc - 1].eventdat2 - (mapX - 3) * 24 - 12;
			else
				enemy[b - 1].ex = eventRec[eventLoc - 1].eventdat2 - mapX3 * 24 + 6;
			enemy[b - 1].ey -= backMove3;

			if (background3x1b)
				enemy[b-1].ex -= 6;
			break;
		}
		enemy[b-1].ey = -28;
		if (background3x1b && enemyOffset == 50)
			enemy[b-1].ey += 4;
	}

	if (smallEnemyAdjust && enemy[b-1].size == 0)
	{
		enemy[b-1].ex -= 10;
		enemy[b-1].ey -= 7;
	}

	enemy[b - 1].ey += eventRec[eventLoc - 1].eventdat5;
	enemy[b - 1].eyc += eventRec[eventLoc - 1].eventdat3;
	enemy[b - 1].linknum = eventRec[eventLoc - 1].eventdat4;
	enemy[b - 1].fixedmovey = eventRec[eventLoc - 1].eventdat6;
	enemy[b - 1].fixedmovey_carry = 0;
	enemy[b - 1].fixedmovey_carry_base = 0;
	enemy[b - 1].fixedmovey_carry_move = 0;
	enemy[b - 1].ey += event_enemy_scroll_catchup(enemyOffset, &enemy[b - 1]);
}

void JE_eventJump(JE_word jump)
{
	JE_word tempW;
	JE_word target;

	if (jump == 65535)
	{
		curLoc = returnLoc;
		target = returnLoc;
	}
	else
	{
		returnLoc = curLoc + 1;
		// Preserve the boosted tick's consumed terrain when rebasing a level-script jump.
		int excess = (int)curLoc - (int)eventRec[eventLoc - 1].eventtime;
		if (excess > endlessScrollExtraPx1)
			excess = endlessScrollExtraPx1;
		if (excess < 0)
			excess = 0;
		curLoc = (JE_word)(jump + excess);
		target = jump;
	}
	// Rescan against the author's target, not the re-based clock: records inside the excess
	// window must still fire (this pass), merely late, like any other boost-overrun record.
	tempW = 0;
	do
	{
		tempW++;
	} while (!(eventRec[tempW-1].eventtime >= target));
	eventLoc = tempW - 1;
}

bool JE_searchFor/*enemy*/(JE_byte PLType, JE_byte* out_index)
{
	int found_id = -1;

	for (int i = 0; i < 100; i++)
	{
		if (enemyAvail[i] == 0 && enemy[i].linknum == PLType)
		{
			found_id = i;
			if (galagaMode)
				enemy[i].evalue += enemy[i].evalue;
		}
	}

	if (found_id != -1)
	{
		if (out_index)
			*out_index = found_id;
		return true;
	}
	else
	{
		return false;
	}
}

void JE_eventSystem(void)
{
	switch (eventRec[eventLoc-1].eventtype)
	{
	case 1:
		starfield_speed = eventRec[eventLoc-1].eventdat;
		break;

	case 2:
		map1YDelay = 1;
		map1YDelayMax = 1;
		map2YDelay = 1;
		map2YDelayMax = 1;

		backMove = eventRec[eventLoc-1].eventdat;
		backMove2 = eventRec[eventLoc-1].eventdat2;

		if (backMove2 > 0)
			explodeMove = backMove2;
		else
			explodeMove = backMove;

		backMove3 = eventRec[eventLoc-1].eventdat3;

		if (backMove > 0)
			stopBackgroundNum = 0;
		break;

	case 3:
		backMove = 1;
		map1YDelay = 3;
		map1YDelayMax = 3;
		backMove2 = 1;
		map2YDelay = 2;
		map2YDelayMax = 2;
		backMove3 = 1;
		break;

	case 4: // Map stop
	case 83: // T2000: Also a map stop 
		stopBackgrounds = true;
		switch (eventRec[eventLoc-1].eventdat)
		{
		case 0:
		case 1:
			stopBackgroundNum = 1;
			break;
		case 2:
			stopBackgroundNum = 2;
			break;
		case 3:
			stopBackgroundNum = 3;
			break;
		}
		break;

	case 5:  // load enemy shape banks
		{
			const Uint8 newEnemyShapeTables[] =
			{
				eventRec[eventLoc-1].eventdat > 0 ? eventRec[eventLoc-1].eventdat : 0,
				eventRec[eventLoc-1].eventdat2 > 0 ? eventRec[eventLoc-1].eventdat2 : 0,
				eventRec[eventLoc-1].eventdat3 > 0 ? eventRec[eventLoc-1].eventdat3 : 0,
				eventRec[eventLoc-1].eventdat4 > 0 ? eventRec[eventLoc-1].eventdat4 : 0,
			};
			
			for (unsigned int i = 0; i < COUNTOF(newEnemyShapeTables); ++i)
			{
				if (enemySpriteSheetIds[i] != newEnemyShapeTables[i])
				{
					if (newEnemyShapeTables[i] > 0)
					{
						assert(newEnemyShapeTables[i] <= COUNTOF(shapeFile));
						JE_loadCompShapes(&enemySpriteSheets[i], shapeFile[newEnemyShapeTables[i] - 1]);
					}
					else
						free_sprite2s(&enemySpriteSheets[i]);

					enemySpriteSheetIds[i] = newEnemyShapeTables[i];
				}
			}
		}
		break;

	case 6: /* Ground Enemy */
		JE_createNewEventEnemy(0, 25, 0);
		break;

	case 7: /* Top Enemy */
		JE_createNewEventEnemy(0, 50, 0);
		break;

	case 8:
		starActive = false;
		break;

	case 9:
		starActive = true;
		break;

	case 10: /* Ground Enemy 2 */
		JE_createNewEventEnemy(0, 75, 0);
		break;

	case 11:
		if (allPlayersGone || eventRec[eventLoc-1].eventdat == 1)
		{
			reallyEndLevel = true;
		}
		else if (!endLevel)
		{
			readyToEndLevel = false;
			endLevel = true;
			levelEnd = 40;
		}
		break;

	case 12: /* Custom 4x4 Ground Enemy */
		{
			uint temp = 0;
			switch (eventRec[eventLoc-1].eventdat6)
			{
			case 0:
			case 1:
				temp = 25;
				break;
			case 2:
				temp = 0;
				break;
			case 3:
				temp = 50;
				break;
			case 4:
				temp = 75;
				break;
			}
			eventRec[eventLoc-1].eventdat6 = 0;   /* We use EVENTDAT6 for the background */
			JE_createNewEventEnemy(0, temp, 0);
			JE_createNewEventEnemy(1, temp, 0);
			if (b > 0)
				enemy[b-1].ex += 24;
			JE_createNewEventEnemy(2, temp, 0);
			if (b > 0)
				enemy[b-1].ey -= 28;
			JE_createNewEventEnemy(3, temp, 0);
			if (b > 0)
			{
				enemy[b-1].ex += 24;
				enemy[b-1].ey -= 28;
			}
			break;
		}
	case 13:
		enemiesActive = false;
		break;

	case 14:
		enemiesActive = true;
		break;

	case 15: /* Sky Enemy */
		JE_createNewEventEnemy(0, 0, 0);
		break;

	case 16:
		if (eventRec[eventLoc-1].eventdat > 9)
		{
			fprintf(stderr, "warning: event 16: bad event data\n");
		}
		else
		{
			JE_drawTextWindow(outputs[eventRec[eventLoc-1].eventdat-1]);
			soundQueue[3] = windowTextSamples[eventRec[eventLoc-1].eventdat-1];
		}
		break;

	case 17: /* Ground Bottom */
		JE_createNewEventEnemy(0, 25, 0);
		if (b > 0)
		{
			enemy[b-1].ey = 190 + eventRec[eventLoc-1].eventdat5;
			enemy[b-1].ey += event_enemy_scroll_catchup(25, &enemy[b-1]);
		}
		break;

	case 18: /* Sky Enemy on Bottom */
		JE_createNewEventEnemy(0, 0, 0);
		if (b > 0)
			enemy[b-1].ey = 190 + eventRec[eventLoc-1].eventdat5;
		break;

	case 19: /* Enemy Global Move */
	{
		int initial_i = 0, max_i = 0;
		bool all_enemies = false;

		if (eventRec[eventLoc-1].eventdat3 > 79 && eventRec[eventLoc-1].eventdat3 < 90)
		{
			initial_i = 0;
			max_i = 100;
			all_enemies = false;
			eventRec[eventLoc-1].eventdat4 = newPL[eventRec[eventLoc-1].eventdat3 - 80];
		}
		else
		{
			switch (eventRec[eventLoc-1].eventdat3)
			{
			case 0:
				initial_i = 0;
				max_i = 100;
				all_enemies = false;
				break;
			case 2:
				initial_i = 0;
				max_i = 25;
				all_enemies = true;
				break;
			case 1:
				initial_i = 25;
				max_i = 50;
				all_enemies = true;
				break;
			case 3:
				initial_i = 50;
				max_i = 75;
				all_enemies = true;
				break;
			case 99:
				initial_i = 0;
				max_i = 100;
				all_enemies = true;
				break;
			}
		}

		for (int i = initial_i; i < max_i; i++)
		{
			if (all_enemies || enemy[i].linknum == eventRec[eventLoc-1].eventdat4)
			{
				if (eventRec[eventLoc-1].eventdat != -99)
					enemy[i].exc = eventRec[eventLoc-1].eventdat;

				if (eventRec[eventLoc-1].eventdat2 != -99)
					enemy[i].eyc = eventRec[eventLoc-1].eventdat2;

				if (eventRec[eventLoc-1].eventdat6 != 0)
				{
					enemy[i].fixedmovey = (eventRec[eventLoc-1].eventdat6 == -99)
					                       ? 0 : eventRec[eventLoc-1].eventdat6;
					enemy[i].fixedmovey_carry = 0;
					enemy[i].fixedmovey_carry_base = 0;
					enemy[i].fixedmovey_carry_move = 0;
				}

				if (eventRec[eventLoc-1].eventdat5 > 0)
					enemy[i].enemycycle = eventRec[eventLoc-1].eventdat5;
			}
		}
		break;
	}

	case 20: /* Enemy Global Accel */
		if (eventRec[eventLoc-1].eventdat3 > 79 && eventRec[eventLoc-1].eventdat3 < 90)
			eventRec[eventLoc-1].eventdat4 = newPL[eventRec[eventLoc-1].eventdat3 - 80];

		for (temp = 0; temp < 100; temp++)
		{
			if (enemyAvail[temp] != 1 &&
			    (enemy[temp].linknum == eventRec[eventLoc-1].eventdat4 || eventRec[eventLoc-1].eventdat4 == 0))
			{
				if (eventRec[eventLoc-1].eventdat != -99)
				{
					enemy[temp].excc = eventRec[eventLoc-1].eventdat;
					enemy[temp].exccw = abs(eventRec[eventLoc-1].eventdat);
					enemy[temp].exccwmax = abs(eventRec[eventLoc-1].eventdat);
					if (eventRec[eventLoc-1].eventdat > 0)
						enemy[temp].exccadd = 1;
					else
						enemy[temp].exccadd = -1;
				}

				if (eventRec[eventLoc-1].eventdat2 != -99)
				{
					enemy[temp].eycc = eventRec[eventLoc-1].eventdat2;
					enemy[temp].eyccw = abs(eventRec[eventLoc-1].eventdat2);
					enemy[temp].eyccwmax = abs(eventRec[eventLoc-1].eventdat2);
					if (eventRec[eventLoc-1].eventdat2 > 0)
						enemy[temp].eyccadd = 1;
					else
						enemy[temp].eyccadd = -1;
				}

				if (eventRec[eventLoc-1].eventdat5 > 0)
				{
					enemy[temp].enemycycle = eventRec[eventLoc-1].eventdat5;
				}
				if (eventRec[eventLoc-1].eventdat6 > 0)
				{
					enemy[temp].ani = eventRec[eventLoc-1].eventdat6;
					enemy[temp].animin = eventRec[eventLoc-1].eventdat5;
					enemy[temp].animax = 0;
					enemy[temp].aniactive = 1;
				}
			}
		}
		break;

	case 21:
		background3over = 1;
		break;

	case 22:
		background3over = 0;
		break;

	case 23: /* Sky Enemy on Bottom */
		JE_createNewEventEnemy(0, 50, 0);
		if (b > 0)
		{
			enemy[b-1].ey = 180 + eventRec[eventLoc-1].eventdat5;
			enemy[b-1].ey += event_enemy_scroll_catchup(50, &enemy[b-1]);
		}
		break;

	case 24: /* Enemy Global Animate */
		for (temp = 0; temp < 100; temp++)
		{
			if (enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
			{
				enemy[temp].aniactive = 1;
				enemy[temp].aniwhenfire = 0;
				if (eventRec[eventLoc-1].eventdat2 > 0)
				{
					enemy[temp].enemycycle = eventRec[eventLoc-1].eventdat2;
					enemy[temp].animin = enemy[temp].enemycycle;
				}
				else
				{
					enemy[temp].enemycycle = 0;
				}

				if (eventRec[eventLoc-1].eventdat > 0)
					enemy[temp].ani = eventRec[eventLoc-1].eventdat;

				if (eventRec[eventLoc-1].eventdat3 == 1)
				{
					enemy[temp].animax = enemy[temp].ani;
				}
				else if (eventRec[eventLoc-1].eventdat3 == 2)
				{
					enemy[temp].aniactive = 2;
					enemy[temp].animax = enemy[temp].ani;
					enemy[temp].aniwhenfire = 2;
				}
			}
		}
		break;

	case 25: /* Enemy Global Damage change */
		for (temp = 0; temp < 100; temp++)
		{
			if (eventRec[eventLoc-1].eventdat4 == 0 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
			{
				const JE_byte wasArmor = enemy[temp].armorleft;
				if (galagaMode)
					enemy[temp].armorleft = roundf(eventRec[eventLoc-1].eventdat * (difficultyLevel / 2));
				else
					enemy[temp].armorleft = eventRec[eventLoc-1].eventdat;
				enemy_note_full_armor(&enemy[temp]);
				enemy_note_armed(temp, wasArmor);
			}
		}
		break;

	case 26:
		smallEnemyAdjust = eventRec[eventLoc-1].eventdat;
		break;

	case 27: /* Enemy Global AccelRev */
		if (eventRec[eventLoc-1].eventdat3 > 79 && eventRec[eventLoc-1].eventdat3 < 90)
			eventRec[eventLoc-1].eventdat4 = newPL[eventRec[eventLoc-1].eventdat3 - 80];

		for (temp = 0; temp < 100; temp++)
		{
			if (eventRec[eventLoc-1].eventdat4 == 0 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
			{
				if (eventRec[eventLoc-1].eventdat != -99)
					enemy[temp].exrev = eventRec[eventLoc-1].eventdat;
				if (eventRec[eventLoc-1].eventdat2 != -99)
					enemy[temp].eyrev = eventRec[eventLoc-1].eventdat2;
				if (eventRec[eventLoc-1].eventdat3 != 0 && eventRec[eventLoc-1].eventdat3 < 17)
					enemy[temp].filter = eventRec[eventLoc-1].eventdat3;
			}
		}
		break;

	case 28:
		topEnemyOver = false;
		break;

	case 29:
		topEnemyOver = true;
		break;

	case 30:
		map1YDelay = 1;
		map1YDelayMax = 1;
		map2YDelay = 1;
		map2YDelayMax = 1;

		backMove = eventRec[eventLoc-1].eventdat;
		backMove2 = eventRec[eventLoc-1].eventdat2;
		explodeMove = backMove2;
		backMove3 = eventRec[eventLoc-1].eventdat3;
		break;

	case 31: /* Enemy Fire Override */
		for (temp = 0; temp < 100; temp++)
		{
			if (eventRec[eventLoc-1].eventdat4 == 99 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
			{
				enemy[temp].freq[1-1] = eventRec[eventLoc-1].eventdat ;
				enemy[temp].freq[2-1] = eventRec[eventLoc-1].eventdat2;
				enemy[temp].freq[3-1] = eventRec[eventLoc-1].eventdat3;
				for (temp2 = 0; temp2 < 3; temp2++)
				{
					enemy[temp].eshotwait[temp2] = 1;
				}
				if (enemy[temp].launchtype > 0)
				{
					enemy[temp].launchfreq = eventRec[eventLoc-1].eventdat5;
					enemy[temp].launchwait = 1;
				}
			}
		}
		break;

	case 32:  // create enemy
		JE_createNewEventEnemy(0, 50, 0);
		if (b > 0)
		{
			enemy[b-1].ey = 190;
			enemy[b-1].ey += event_enemy_scroll_catchup(50, &enemy[b-1]);
		}
		break;

	case 33: /* Enemy From other Enemies */
		if (!((eventRec[eventLoc-1].eventdat == 512 || eventRec[eventLoc-1].eventdat == 513) && (arcade_rules_active() || superTyrian)))
		{
			if (superArcadeMode != SA_NONE)
			{
				if (eventRec[eventLoc-1].eventdat == 534)
					eventRec[eventLoc-1].eventdat = 827;
			}
			else if (!superTyrian)
			{
				const Uint8 lives = *player[0].lives;

				if (eventRec[eventLoc-1].eventdat == 533 && (lives == 11 || (mt_rand() % 15) < lives))
				{
					// enemy will drop random special weapon; in endless, a front/rear powerup
					// or the 5000 gem instead (specials already come from cubes and orbs there)
					eventRec[eventLoc-1].eventdat = endlessMode ? endlessPowerupDropEnemy()
					                                            : 829 + (mt_rand() % 6);
				}
			}
			if (eventRec[eventLoc-1].eventdat == 534 && superTyrian)
				eventRec[eventLoc-1].eventdat = 828 + superTyrianSpecials[mt_rand() % 4];

			for (temp = 0; temp < 100; temp++)
			{
				if (enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
					enemy[temp].enemydie = eventRec[eventLoc-1].eventdat;
			}
		}
		break;

	case 34: /* Start Music Fade */
		// A milestone zone is pinned to its own theme (endlessPickLevelMusic), so the script's
		// fade is ignored too; a level that fades here and swaps tracks at event 35 would
		// otherwise leave the pinned song stuck at the fade floor for the rest of the zone.
		if (firstGameOver && !endlessMilestoneZone())
		{
			musicFade = true;
			tempVolume = tyrMusicVolume;
		}
		break;

	case 35: /* Play new song */
		if (firstGameOver)
		{
			if (!endlessMilestoneZone())  // pinned theme: keep it playing, just restore the volume
				play_song(eventRec[eventLoc-1].eventdat - 1);
			set_volume(tyrMusicVolume, fxVolume);
		}
		musicFade = false;
		break;

	case 36:
		readyToEndLevel = true;
		break;

	case 37:
		levelEnemyFrequency = eventRec[eventLoc-1].eventdat;
		break;

	case 38:
		curLoc = eventRec[eventLoc-1].eventdat;
		int new_event_loc = 1;
		for (tempW = 0; tempW < maxEvent; tempW++)
		{
			if (eventRec[tempW].eventtime <= curLoc)
				new_event_loc = tempW+1 - 1;
		}
		eventLoc = new_event_loc;
		break;

	case 39: /* Enemy Global Linknum Change */
		for (temp = 0; temp < 100; temp++)
		{
			if (enemy[temp].linknum == eventRec[eventLoc-1].eventdat)
				enemy[temp].linknum = eventRec[eventLoc-1].eventdat2;
		}
		break;

	case 40: /* Enemy Continual Damage */
		enemyContinualDamage = true;
		break;

	case 41:
		if (eventRec[eventLoc-1].eventdat == 0)
		{
			memset(enemyAvail, 1, sizeof(enemyAvail));
		}
		else
		{
			for (x = 0; x <= 24; x++)
				enemyAvail[x] = 1;
		}
		break;

	case 42:
		background3over = 2;
		break;

	case 43:
		background2over = eventRec[eventLoc-1].eventdat;
		break;

	case 44:
		filterActive       = (eventRec[eventLoc-1].eventdat > 0);
		filterFade         = (eventRec[eventLoc-1].eventdat == 2);
		levelFilter        = eventRec[eventLoc-1].eventdat2;
		levelBrightness    = eventRec[eventLoc-1].eventdat3;
		levelFilterNew     = eventRec[eventLoc-1].eventdat4;
		levelBrightnessChg = eventRec[eventLoc-1].eventdat5;
		filterFadeStart    = (eventRec[eventLoc-1].eventdat6 == 0);
		flareOwnsFilter    = false;  // the level script's own grade; Special Tint never hides it
		break;

	case 45: /* arcade-only enemy from other enemies */
		if (!superTyrian)
		{
			const Uint8 lives = *player[0].lives;

			if (eventRec[eventLoc-1].eventdat == 533 && (lives == 11 || (mt_rand() % 15) < lives))
			{
				// same endless swap as event 33 (this arcade-only variant never fires in endless,
				// but the two substitutions are otherwise identical; keep them that way)
				eventRec[eventLoc-1].eventdat = endlessMode ? endlessPowerupDropEnemy()
				                                            : 829 + (mt_rand() % 6);
			}
			if (arcade_rules_active())
			{
				for (temp = 0; temp < 100; temp++)
				{
					if (enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
						enemy[temp].enemydie = eventRec[eventLoc-1].eventdat;
				}
			}
		}
		break;

	case 46:  // change difficulty
		if (eventRec[eventLoc-1].eventdat3 != 0)
			damageRate = eventRec[eventLoc-1].eventdat3;

		if ((eventRec[eventLoc-1].eventdat2 == 0 || arcade_rules_active())
		    && difficulty_adjust_active())
		{
			difficultyLevel += eventRec[eventLoc-1].eventdat;
			if (difficultyLevel < DIFFICULTY_EASY)
				difficultyLevel = DIFFICULTY_EASY;
			if (difficultyLevel > DIFFICULTY_10)
				difficultyLevel = DIFFICULTY_10;
		}
		break;

	case 47: /* Enemy Global AccelRev */
		for (temp = 0; temp < 100; temp++)
		{
			if (eventRec[eventLoc-1].eventdat4 == 0 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
			{
				const JE_byte wasArmor = enemy[temp].armorleft;
				enemy[temp].armorleft = eventRec[eventLoc-1].eventdat;
				enemy_note_full_armor(&enemy[temp]);
				enemy_note_armed(temp, wasArmor);
			}
		}
		break;

	case 48: /* Background 2 Cannot be Transparent */
		background2notTransparent = true;
		break;

	case 49:
	case 50:
	case 51:
	case 52:
		tempDat2 = eventRec[eventLoc-1].eventdat;
		eventRec[eventLoc-1].eventdat = 0;
		tempDat = eventRec[eventLoc-1].eventdat3;
		eventRec[eventLoc-1].eventdat3 = 0;
		tempDat3 = eventRec[eventLoc-1].eventdat6;
		eventRec[eventLoc-1].eventdat6 = 0;
		enemyDat[0].armor = tempDat3;
		enemyDat[0].egraphic[1-1] = tempDat2;
		switch (eventRec[eventLoc-1].eventtype - 48)
		{
		case 1:
			temp = 25;
			break;
		case 2:
			temp = 0;
			break;
		case 3:
			temp = 50;
			break;
		case 4:
			temp = 75;
			break;
		}
		JE_createNewEventEnemy(0, temp, tempDat);
		eventRec[eventLoc-1].eventdat = tempDat2;
		eventRec[eventLoc-1].eventdat3 = tempDat;
		eventRec[eventLoc-1].eventdat6 = tempDat3;
		break;

	case 53:
		forceEvents = (eventRec[eventLoc-1].eventdat != 99);
		break;

	case 54:
		JE_eventJump(eventRec[eventLoc-1].eventdat);
		break;

	case 55: /* Enemy Global AccelRev */
		if (eventRec[eventLoc-1].eventdat3 > 79 && eventRec[eventLoc-1].eventdat3 < 90)
			eventRec[eventLoc-1].eventdat4 = newPL[eventRec[eventLoc-1].eventdat3 - 80];

		for (temp = 0; temp < 100; temp++)
		{
			if (eventRec[eventLoc-1].eventdat4 == 0 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
			{
				if (eventRec[eventLoc-1].eventdat != -99)
					enemy[temp].xaccel = eventRec[eventLoc-1].eventdat;
				if (eventRec[eventLoc-1].eventdat2 != -99)
					enemy[temp].yaccel = eventRec[eventLoc-1].eventdat2;
			}
		}
		break;

	case 56: /* Ground2 Bottom */
		JE_createNewEventEnemy(0, 75, 0);
		if (b > 0)
		{
			enemy[b-1].ey = 190;
			enemy[b-1].ey += event_enemy_scroll_catchup(75, &enemy[b-1]);
		}
		break;

	case 57:
		superEnemy254Jump = eventRec[eventLoc-1].eventdat;
		break;

	case 58: // Set enemy launch
		// This implementation comes from ArcTyr, and may not be 100% accurate to Tyrian 2000
		for (temp = 0; temp < 100; temp++)
		{
			if (eventRec[eventLoc-1].eventdat4 == 99 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
				enemy[temp].launchtype = eventRec[eventLoc-1].eventdat;
		}
		break;

	case 59: // Replace enemy
	case 68: // Note: random explosions got moved to event 99 in T2000
		// This implementation comes from ArcTyr, and may not be 100% accurate to Tyrian 2000
		{
			Uint16 eDatI = eventRec[eventLoc-1].eventdat;

			for (temp = 0; temp < 100; temp++)
			{
				if (!(eventRec[eventLoc-1].eventdat4 == 0 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4))
					continue;

				const int enemy_offset = temp - (temp % 25);
				b = JE_newEnemy(enemy_offset, eDatI, 0);
				if (b != 0)
				{
					enemy[b-1].ex = enemy[temp].ex;
					enemy[b-1].ey = enemy[temp].ey;
				}

				enemyAvail[temp] = 1;
			}			
		}
		break;
		break;

	case 60: /*Assign Special Enemy*/
		for (temp = 0; temp < 100; temp++)
		{
			if (enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
			{
				enemy[temp].special = true;
				enemy[temp].flagnum = eventRec[eventLoc-1].eventdat;
				enemy[temp].setto  = (eventRec[eventLoc-1].eventdat2 == 1);
			}
		}
		break;

	case 61:  // if specific flag set to specific value, skip events
		if (globalFlags[eventRec[eventLoc-1].eventdat-1] == eventRec[eventLoc-1].eventdat2)
			eventLoc += eventRec[eventLoc-1].eventdat3;
		break;

	case 62: /*Play sound effect*/
		soundQueue[3] = eventRec[eventLoc-1].eventdat;
		break;

	case 63:  // skip events if not in 2-player mode
		if (!arcade_rules_active())
			eventLoc += eventRec[eventLoc-1].eventdat;
		break;

	case 64:
		if (!(eventRec[eventLoc-1].eventdat == 6 && split_arcade_mode() && difficultyLevel > DIFFICULTY_NORMAL))
		{
			smoothies[eventRec[eventLoc-1].eventdat-1] = eventRec[eventLoc-1].eventdat2;
			temp = eventRec[eventLoc-1].eventdat;
			if (temp == 5)
				temp = 3;
			smoothie_data[temp-1] = eventRec[eventLoc-1].eventdat3;
		}
		break;

	case 65:
		background3x1 = (eventRec[eventLoc-1].eventdat == 0);
		break;

	case 66: /*If not on this difficulty level or higher then...*/
		if (initialDifficulty <= eventRec[eventLoc-1].eventdat)
			eventLoc += eventRec[eventLoc-1].eventdat2;
		break;

	case 67:
		levelTimer = (eventRec[eventLoc-1].eventdat == 1);
		levelTimerCountdown = eventRec[eventLoc-1].eventdat3 * 100;
		levelTimerJumpTo   = eventRec[eventLoc-1].eventdat2;
		break;

	case 69:
		for (uint i = 0; i < COUNTOF(player); ++i)
			player[i].invulnerable_ticks = eventRec[eventLoc-1].eventdat;
		break;

	case 70:
		if (eventRec[eventLoc-1].eventdat2 == 0)
		{  /*1-10*/
			bool found = false;

			for (temp = 1; temp <= 19; temp++)
				found = found || JE_searchFor(temp, NULL);

			if (!found)
				JE_eventJump(eventRec[eventLoc-1].eventdat);
		}
		else if (!JE_searchFor(eventRec[eventLoc-1].eventdat2, NULL) &&
		         (eventRec[eventLoc-1].eventdat3 == 0 || !JE_searchFor(eventRec[eventLoc-1].eventdat3, NULL)) &&
		         (eventRec[eventLoc-1].eventdat4 == 0 || !JE_searchFor(eventRec[eventLoc-1].eventdat4, NULL)))
		{
			JE_eventJump(eventRec[eventLoc-1].eventdat);
		}
		break;

	case 71:
		if (((((intptr_t)mapYPos - (intptr_t)&megaData1.mainmap) / sizeof(JE_byte *)) * 2) <= (unsigned)eventRec[eventLoc-1].eventdat2)
			JE_eventJump(eventRec[eventLoc-1].eventdat);
		break;

	case 72:
		background3x1b = (eventRec[eventLoc-1].eventdat == 1);
		break;

	case 73:
		skyEnemyOverAll = (eventRec[eventLoc-1].eventdat == 1);
		break;

	case 74: /* Enemy Global BounceParams */
		for (temp = 0; temp < 100; temp++)
		{
			if (eventRec[eventLoc-1].eventdat4 == 0 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
			{
				if (eventRec[eventLoc-1].eventdat5 != -99)
					enemy[temp].xminbounce = eventRec[eventLoc-1].eventdat5;

				if (eventRec[eventLoc-1].eventdat6 != -99)
					enemy[temp].yminbounce = eventRec[eventLoc-1].eventdat6;

				if (eventRec[eventLoc-1].eventdat != -99)
					// Bounce data was authored for the 320px field; shift the right bound
					// out by the widescreen extension so enemies sweep the full playfield.
					enemy[temp].xmaxbounce = eventRec[eventLoc-1].eventdat + (vga_width - LEGACY_WIDTH);

				if (eventRec[eventLoc-1].eventdat2 != -99)
					enemy[temp].ymaxbounce = eventRec[eventLoc-1].eventdat2;
			}
		}
		break;

	case 75:;
		bool temp_no_clue = false; // whether the requested link range contains a live stationary enemy

		for (temp = 0; temp < 100; temp++)
		{
			if (enemyAvail[temp] == 0 &&
			    enemy[temp].eyc == 0 &&
			    enemy[temp].linknum >= eventRec[eventLoc-1].eventdat &&
			    enemy[temp].linknum <= eventRec[eventLoc-1].eventdat2)
			{
				temp_no_clue = true;
			}
		}

		if (temp_no_clue)
		{
			JE_byte enemy_i;
			do
			{
				temp = (mt_rand() % (eventRec[eventLoc-1].eventdat2 + 1 - eventRec[eventLoc-1].eventdat)) + eventRec[eventLoc-1].eventdat;
			} while (!(JE_searchFor(temp, &enemy_i) && enemy[enemy_i].eyc == 0));

			newPL[eventRec[eventLoc-1].eventdat3 - 80] = temp;
		}
		else
		{
			newPL[eventRec[eventLoc-1].eventdat3 - 80] = 255;
			if (eventRec[eventLoc-1].eventdat4 > 0)
			{ /*Skip*/
				curLoc = eventRec[eventLoc-1 + eventRec[eventLoc-1].eventdat4].eventtime - 1;
				eventLoc += eventRec[eventLoc-1].eventdat4 - 1;
			}
		}

		break;

	case 76:
		returnActive = true;
		break;

	case 77:
		mapYPos = &megaData1.mainmap[0][0];
		mapYPos += eventRec[eventLoc-1].eventdat / 2;
		if (eventRec[eventLoc-1].eventdat2 > 0)
		{
			mapY2Pos = &megaData2.mainmap[0][0];
			mapY2Pos += eventRec[eventLoc-1].eventdat2 / 2;
		}
		else
		{
			mapY2Pos = &megaData2.mainmap[0][0];
			mapY2Pos += eventRec[eventLoc-1].eventdat / 2;
		}
		break;

	case 78:
		if (galagaShotFreq < 10)
			galagaShotFreq++;
		break;

	case 79:
		boss_bar[0].link_num = eventRec[eventLoc - 1].eventdat;
		boss_bar[1].link_num = eventRec[eventLoc - 1].eventdat2;
		break;

	case 80:  // skip events if in 2-player mode
		if (split_arcade_mode())
			eventLoc += eventRec[eventLoc-1].eventdat;
		break;

	case 81: /*WRAP2*/
		BKwrap2   = &megaData2.mainmap[0][0];
		BKwrap2   += eventRec[eventLoc-1].eventdat / 2;
		BKwrap2to = &megaData2.mainmap[0][0];
		BKwrap2to += eventRec[eventLoc-1].eventdat2 / 2;
		break;

	case 82: /*Give SPECIAL WEAPON*/
		for (uint p = 0; p < (dual_ship_mode() ? COUNTOF(player) : 1u); ++p)
		{
			player[p].items.special = eventRec[eventLoc-1].eventdat;
			if (dual_ship_mode())
			{
				player[p].shot_multi_pos[SHOT_SPECIAL] = 0;
				player[p].shot_repeat[SHOT_SPECIAL] = 0;
				player[p].shot_multi_pos[SHOT_SPECIAL2] = 0;
				player[p].shot_repeat[SHOT_SPECIAL2] = 0;
			}
			hud_special_light_rearm(p);
		}
		shotMultiPos[SHOT_SPECIAL] = 0;
		shotRepeat[SHOT_SPECIAL] = 0;
		shotMultiPos[SHOT_SPECIAL2] = 0;
		shotRepeat[SHOT_SPECIAL2] = 0;
		break;

	case 84: // timed battle level timer
		if (!timedBattleMode)
			break;

		// note: a copy of event 67
		levelTimer = (eventRec[eventLoc-1].eventdat == 1);
		levelTimerCountdown = eventRec[eventLoc-1].eventdat3 * 100;
		levelTimerJumpTo   = eventRec[eventLoc-1].eventdat2;
		break;

	case 85: // timed battle enemy from other enemies
		if (timedBattleMode)
		{
			for (temp = 0; temp < 100; temp++)
			{
				if (enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
					enemy[temp].enemydie = eventRec[eventLoc-1].eventdat;
			}
		}
		break;


	case 99:
		randomExplosions = (eventRec[eventLoc-1].eventdat == 1);
		break;

	default:
		fprintf(stderr, "warning: ignoring unknown event %d\n", eventRec[eventLoc-1].eventtype);
		break;
	}

	eventLoc++;
}

void JE_whoa(void)
{
	unsigned int i, j, color, offset, timer;
	unsigned int screenSize, topBorder, bottomBorder;
	Uint8 * TempScreen1, * TempScreen2, * TempScreenSwap;

	/* Fade used by the "engage" code. Existing secondary screens provide the
	 * two temporary buffers. */

	TempScreen1  = game_screen->pixels;
	TempScreen2  = VGAScreen2->pixels;

	screenSize   = VGAScreenSeg->h * VGAScreenSeg->pitch;
	topBorder    = VGAScreenSeg->pitch * 4; /* Four excluded rows. */
	bottomBorder = VGAScreenSeg->pitch * 7;

	/* Both temporary surfaces must cover the active surface. */
	assert((unsigned)VGAScreen2->h  * VGAScreen2->pitch >= screenSize &&
	       (unsigned)game_screen->h * game_screen->pitch >= screenSize);

	/* Exclude the top and bottom borders from processing and drawing. */
	memset((Uint8 *)VGAScreenSeg->pixels, 0, topBorder);
	memset((Uint8 *)VGAScreenSeg->pixels + screenSize - bottomBorder, 0, bottomBorder);

	/* Seed one temporary buffer with the screen and clear the other. */
	memset(TempScreen1, 0, screenSize);
	memcpy(TempScreen2, VGAScreenSeg->pixels, VGAScreenSeg->h * VGAScreenSeg->pitch);

	service_SDL_events(true);
	timer = 300; /* Enough diffusion rounds to leave the screen nearly black. */

	do
	{
		setDelay(1);

		/* Diffuse neighboring pixels into the fading trail. */
		for (i = screenSize - bottomBorder, j = topBorder / 2; i > 0; i--, j++)
		{
			offset = j + i/8192 - 4;
			color = (TempScreen2[offset                    ] * 12 +
			         TempScreen1[offset-VGAScreenSeg->pitch]      +
			         TempScreen1[offset-1                  ]      +
			         TempScreen1[offset+1                  ]      +
			         TempScreen1[offset+VGAScreenSeg->pitch]) / 16;

			TempScreen1[j] = color;
		}

		/* Copy the next diffusion frame to the display buffer. */
		memcpy((Uint8 *)VGAScreenSeg->pixels + topBorder, TempScreen1 + topBorder, screenSize - bottomBorder);

		JE_showVGA();

		timer--;
		wait_delay();

		/* Flip the buffer. */
		TempScreenSwap = TempScreen1;
		TempScreen1    = TempScreen2;
		TempScreen2    = TempScreenSwap;

	} while (!(timer == 0 || JE_anyButton()));

	levelWarningLines = 4;
}

static void JE_barX(JE_word x1, JE_word y1, JE_word x2, JE_word y2, JE_byte col)
{
	fill_rectangle_xy(VGAScreen, x1, y1,     x2, y1,     col + 1);
	fill_rectangle_xy(VGAScreen, x1, y1 + 1, x2, y2 - 1, col    );
	fill_rectangle_xy(VGAScreen, x1, y2,     x2, y2,     col - 1);
}

#define BOSS_BAR_GREY_BANK ENEMY_FLASH_BANK

/* Return the link group's bar palette and flash lift. Invulnerable groups use the grey bank. */
static void boss_bar_colours(JE_byte link_num, int *base, int *lift)
{
	int tier = 0;              // highest endless tier among the group's live parts
	unsigned int armor = 256;  // its most-damaged part, on boss_bar_survey's terms
	Uint32 cue = 0;            // frames left on the youngest arming cue among them

	if (link_num != 0)
	{
		for (unsigned int e = 0; e < COUNTOF(enemy); e++)
		{
			if (enemyAvail[e] != 0 || enemy[e].linknum != link_num)
				continue;

			if (enemy[e].eliteState > tier)
				tier = enemy[e].eliteState;
			if (enemy[e].armorleft < armor)
				armor = enemy[e].armorleft;

			const Uint32 left = enemy_armed_flash_left(e);
			if (left > cue)
				cue = left;
		}
	}

	*lift = (vulnerableCue != VULN_CUE_OFF) ? (int)cue : 0;

	if (vulnerableCue != VULN_CUE_OFF && armor == 255)
	{
		*base = BOSS_BAR_GREY_BANK;
		return;
	}

	const Uint8 tint = endlessFxActive() ? endlessEliteTint(tier) : 0;
	*base = (tint != 0) ? tint : 112;  // palette bank 7 (default)
}

static void bbfill(SDL_Surface *dst, int x0, int y0, int x1, int y1, int scale, Uint8 color)
{
	fill_rectangle_xy(dst, x0 * scale, y0 * scale, (x1 + 1) * scale - 1, (y1 + 1) * scale - 1, color);
}

// One enhanced boss bar: a framed, recessed track with a glossy gradient fill. (gx,gy)/gw/gh are
// the outer frame; horizontal fills left->right, vertical bottom->up; fraction is 0..1; flash
// brightens on a hit. Colors stay within the supplied palette bank.
#define BOSS_BAR_MIN_SIDE 4   // a frame any smaller draws nothing at all

// Mark only boss bars large enough to draw; marking an empty rectangle freezes its background.
static void boss_bar_mark_overlay(int gx, int gy, int gw, int gh)
{
	if (gw >= BOSS_BAR_MIN_SIDE && gh >= BOSS_BAR_MIN_SIDE)
		rl_mark_overlay_rect(gx, gy, gw, gh);
}

static void draw_boss_bar_gauge(SDL_Surface *dst, int scale, int gx, int gy, int gw, int gh,
                                bool horizontal, float fraction, int flash, int base)
{
	if (gw < BOSS_BAR_MIN_SIDE || gh < BOSS_BAR_MIN_SIDE)
		return;
	if (fraction < 0.0f)
		fraction = 0.0f;
	else if (fraction > 1.0f)
		fraction = 1.0f;

	const int BASE  = base;        // palette bank 7 normally; elite/champion tint in endless
	const int FRAME = BASE + 6;    // visible outline
	const int TRACK = BASE + 2;    // dark empty groove

	const int ix = gx + 1, iy = gy + 1;     // inner track origin
	const int iw = gw - 2, ih = gh - 2;     // inner track size
	const int cross = horizontal ? ih : iw; // bar thickness (across the fill)
	const int along = horizontal ? iw : ih; // bar length (along the fill)

	// Outline + recessed empty groove (always drawn, so a depleted bar still reads).
	bbfill(dst, gx, gy, gx + gw - 1, gy + gh - 1, scale, (Uint8)FRAME);
	bbfill(dst, ix, iy, ix + iw - 1, iy + ih - 1, scale, (Uint8)TRACK);

	const int fillLen = (int)(along * fraction + 0.5f);
	if (fillLen <= 0)
		return;  // boss at (near) zero: just the empty groove shows

	// Glossy fill: a brightness gradient across the thickness (highlight near the
	// first edge, darker at the far edge), lifted by the hit-flash amount.
	for (int c = 0; c < cross; ++c)
	{
		int shade = (cross <= 1) ? 13 : 15 - (c * 6) / (cross - 1);  // 127 -> ~121
		int col = BASE + shade + flash;
		if (col > BASE + 15)
			col = BASE + 15;

		if (horizontal)
			bbfill(dst, ix, iy + c, ix + fillLen - 1, iy + c, scale, (Uint8)col);
		else
			bbfill(dst, ix + c, iy + ih - fillLen, ix + c, iy + ih - 1, scale, (Uint8)col);
	}

	// Bright leading-edge cap at the tip of the fill for a crisp, readable edge.
	if (horizontal)
		bbfill(dst, ix + fillLen - 1, iy, ix + fillLen - 1, iy + ih - 1, scale, (Uint8)(BASE + 15));
	else
		bbfill(dst, ix, iy + ih - fillLen, ix + iw - 1, iy + ih - fillLen, scale, (Uint8)(BASE + 15));
}

// Shared by draw_boss_bars_enhanced and boss_bar_right_edge_x (below), so the two can never
// drift apart: the enhanced gauge's thickness and the gap between two grouped bars.
static const int BOSS_BAR_THICK = 7;
static const int BOSS_BAR_GAP   = 4;

static int boss_flash_render(int color, float alpha)
{
	if (color <= 0)
		return 0;
	int f = (int)(color + 1.0f - alpha + 0.5f);
	if (f < 0)
		f = 0;
	return f;
}

// Blank rows or columns left between a bar's frame and the nearest HUD ink.
#define BOSS_BAR_CLEAR 2

void boss_bar_vertical_span(bool onLeft, int *top, int *bot)
{
	// The side bars are tuned a row lower than that shared clearance: three blank rows above
	// the frame and one below. The trailing step is off the neighbour's own inked row.
	const int aboveGap = BOSS_BAR_CLEAR + 1;
	const int belowGap = BOSS_BAR_CLEAR - 1;
	const int topMin = 7;    // three blank rows under the low-armor WARNING strip (rows 0..3)
	const int botMax = 176;  // one above the WARNING text's first row

	int vTop = (onLeft ? hud_top_left_bottom_edge() : hud_top_right_bottom_edge())
	         + aboveGap + 1;
	if (vTop < topMin)
		vTop = topMin;
	int vBot = (onLeft ? hud_bottom_left_top_edge() : hud_bottom_right_top_edge())
	         - belowGap - 1;
	if (vBot > botMax)
		vBot = botMax;

	*top = vTop;
	*bot = vBot;
}

// Lay out and draw the enhanced boss bars per the player's Enhancements
// settings (bossBarLayout / bossBarTwoMode). barCount is 1 or 2.
static void draw_boss_bars_enhanced(SDL_Surface *dst, int scale, float flashAlpha, bool decrement, unsigned int barCount)
{
	// Bars draw into game_screen (playfield space); JE_inGameDisplays draws the corner HUD
	// indicators in the same space, so keep bars centred on the playfield and clear of them.
	const int PF_L  = PLAYFIELD_LEFT;        // 24: left visible edge
	const int PF_R  = PLAYFIELD_RIGHT;       // 322: right visible edge, just before the HUD
	const int PF_CX = PF_L + PLAYFIELD_WIDTH / 2;    // 173: playfield centre
	const bool two  = (barCount == 2);

	const int THICK = BOSS_BAR_THICK;   // bar thickness
	const int GAP   = BOSS_BAR_GAP;     // spacing between two grouped bars

	const bool vertical  = (bossBarLayout == BOSS_BAR_LEFT || bossBarLayout == BOSS_BAR_RIGHT);
	const bool splitMode = (bossBarTwoMode == BOSS_BAR_TWO_SPLIT);
	const bool stackMode = (bossBarTwoMode == BOSS_BAR_TWO_STACKED);

	if (!vertical)
	{
		// Horizontal bars.
		const bool top = (bossBarLayout == BOSS_BAR_TOP);
		const bool sideBySide = two && splitMode;  // halves on one row; else stacked rows

		// Top bars clear the live corner HUD clusters, bottom bars the playfield edges. Only
		// the right edge steps off an inked column; the left one already reports the first
		// free column past its cluster.
		const int fieldL = PF_L + BOSS_BAR_CLEAR;
		const int fieldR = PF_R - BOSS_BAR_CLEAR;
		int leftClear  = top ? (hud_top_left_right_edge() + BOSS_BAR_CLEAR) : fieldL;
		int rightClear = top ? (hud_top_right_left_edge() - BOSS_BAR_CLEAR - 1) : fieldR;

		// With no corner cluster on that side the measured edge falls outside the playfield,
		// so pin both limits to the visible area.
		if (leftClear < fieldL)   leftClear = fieldL;
		if (rightClear > fieldR)  rightClear = fieldR;
		const int leftHalf   = PF_CX - leftClear;
		const int rightHalf  = rightClear - PF_CX;
		const int half       = (leftHalf < rightHalf) ? leftHalf : rightHalf;
		const int fullL      = PF_CX - half;
		const int fullR      = PF_CX + half;
		const int fullW      = fullR - fullL + 1;

		// Clear the level timer at top and the current score/FPS band at bottom.
		const int topAnchor = levelTimer ? 18 : 6;
		const int botAnchor = hud_bottom_band_top() - BOSS_BAR_CLEAR - 1;

		for (unsigned int b = 0; b < barCount; b++)
		{
			int bx = fullL, bw = fullW, by;

			if (sideBySide)
			{
				bw = (fullW - GAP) / 2;
				bx = (b == 0) ? fullL : fullR - bw + 1;
			}

			if (top)
				by = topAnchor + ((two && !sideBySide) ? (int)b * (THICK + GAP) : 0);
			else  // bottom: stacked grows upward, bar 0 on top
				by = botAnchor - THICK + 1
				   - ((two && !sideBySide) ? (int)(barCount - 1 - b) * (THICK + GAP) : 0);

			if (decrement)  // the authoritative tick draw
				boss_bar_mark_overlay(bx, by, bw, THICK);

			int base, lift;
			boss_bar_colours(boss_bar[b].link_num, &base, &lift);
			const int hit = boss_flash_render(boss_bar[b].color, flashAlpha);

			draw_boss_bar_gauge(dst, scale, bx, by, bw, THICK, true,
			                    boss_bar[b].fill / (float)BOSS_BAR_FULL,
			                    (lift > hit) ? lift : hit, base);

			if (decrement && boss_bar[b].color > 0)
				boss_bar[b].color--;
		}
	}
	else
	{
		// Vertical bars hug the side edges. Split uses both sides; Together runs
		// in parallel; Stacked places one above the other. Each side's span comes from
		// the corner HUD actually drawn there this tick (boss_bar_vertical_span), so a
		// clear side gives the full playfield height.
		const int edgeL = PF_L + BOSS_BAR_CLEAR;   // clear columns inside the left edge
		const int edgeR = PF_R - BOSS_BAR_CLEAR;   // ...and before the HUD sidebar

		for (unsigned int b = 0; b < barCount; b++)
		{
			const bool onLeft = (two && splitMode)
				? (b == 0)                              // one bar on each side
				: (bossBarLayout == BOSS_BAR_LEFT);     // single, or both on chosen side

			// Parallel ("Together") offsets the second bar inward; split/stacked share a column.
			const int slot = (two && !splitMode && !stackMode) ? (int)b : 0;

			int bx = onLeft
				? edgeL + slot * (THICK + GAP)
				: edgeR - THICK + 1 - slot * (THICK + GAP);

			int vTop, vBot;
			boss_bar_vertical_span(onLeft, &vTop, &vBot);

			// Stacked: split this side's span into a top half and a bottom half, the upper
			// bar shifted one row further down and the lower one row up.
			if (two && stackMode)
			{
				const int mid = (vTop + vBot) / 2;
				if (b == 0)
				{
					vTop += 1;
					vBot = mid - GAP / 2 + 1;      // upper bar
				}
				else
				{
					vTop = mid + GAP / 2;          // lower bar
					vBot -= 1;
				}
			}

			if (decrement)  // see the horizontal branch
				boss_bar_mark_overlay(bx, vTop, THICK, vBot - vTop + 1);

			int base, lift;
			boss_bar_colours(boss_bar[b].link_num, &base, &lift);
			const int hit = boss_flash_render(boss_bar[b].color, flashAlpha);

			draw_boss_bar_gauge(dst, scale, bx, vTop, THICK, vBot - vTop + 1, false,
			                    boss_bar[b].fill / (float)BOSS_BAR_FULL,
			                    (lift > hit) ? lift : hit, base);

			if (decrement && boss_bar[b].color > 0)
				boss_bar[b].color--;
		}
	}
}

// Original compact double-sided boss bar (kept for the "Classic" setting).
static void draw_boss_bars_classic(unsigned int bars)
{
	const int playfield_left = PLAYFIELD_LEFT;
	const int center_x = playfield_left + PLAYFIELD_WIDTH / 2;

	for (unsigned int b = 0; b < bars; b++)
	{
		unsigned int x;

		if (bars == 2)
			x = center_x + ((b == 0) ? -30 : 30);
		else
			x = center_x + ((levelTimer) ? 95 : 0);  // level timer and boss bar would overlap

		unsigned int y = (levelTimer) ? 15 : 7;

		int base, lift;
		boss_bar_colours(boss_bar[b].link_num, &base, &lift);
		const int flash = (lift > boss_bar[b].color) ? lift : boss_bar[b].color;

		JE_barX(x - 25, y, x + 25, y + 5, base + 3);
		JE_barX(x - (boss_bar[b].fill / 10), y, x + (boss_bar[b].fill + 5) / 10, y + 5,
		        base + 6 + flash);

		if (boss_bar[b].color > 0)
			boss_bar[b].color--;
	}
}

// Draw one enemy-group bar around its on-screen bounds. Settings choose placement
// and opacity; the bank-7 color ramp tracks remaining health.
static void draw_enemy_hp_bar(int id, int boxL, int boxR, int boxT, int boxB, float frac,
                              int barBase, float par_frac, int par_layer, float par_anchor,
                              int par_ybase, float par_yfrac, int par_ylayer)
{
	if (frac < 0.0f) frac = 0.0f; else if (frac > 1.0f) frac = 1.0f;

	const Uint8 opacity = (Uint8)(enemyBarOpacity * 255 / 100);
	if (opacity == 0)
		return;  // fully transparent: nothing to draw or record

	const bool vertical = (enemyBarLayout == ENEMY_BAR_VERTICAL);

	int x, y, along;
	if (!enemy_bar_place(boxL, boxR, boxT, boxB, ENEMY_BAR_THICK, vertical, &x, &y, &along))
		return;

	const int fill = (int)(along * frac + 0.5f);
	// Fill colour tracks remaining health within the bar's palette bank: full -> +15
	// (bright), near-empty -> +5 (dark). barBase is bank 7 (112) normally, or the elite /
	// champion tint bank so a special enemy's bar matches its tint.
	const int col = (fill > 0) ? barBase + 5 + (int)(frac * 10.0f + 0.5f) : barBase;

	// Draw into the authoritative tick frame.
	rl_draw_hp_bar(VGAScreen, x, y, along, fill, (Uint8)col, vertical, opacity, 0);

	// Record the bar so the replay reproduces AND interpolates it with its enemy
	// (id = RL_ID_ENEMYBAR_BASE + slot). It stays out of the residual via recording
	// (normal levels) / pre-snapshot call order (smoothie levels); see the call sites.
	if (render_list_recording)
	{
		rl_current_id = id;
		rl_current_par_frac = par_frac;    // float the bar's parallax to match its enemy
		rl_current_par_layer = par_layer;
		rl_current_par_anchor = par_anchor;
		rl_current_par_ybase = par_ybase;
		rl_current_par_yfrac = par_yfrac;  // float the bar's vertical scroll to match its enemy
		rl_current_par_ylayer = par_ylayer;
		rl_rec_hp_bar(x, y, along, fill, (Uint8)col, vertical, opacity, 0);
		rl_current_id = 0;
		rl_current_par_frac = 0.0f;
		rl_current_par_layer = 0;
		rl_current_par_anchor = 0.0f;
		rl_current_par_ybase = 0;
		rl_current_par_yfrac = 0.0f;
		rl_current_par_ylayer = 0;
	}
}

// Tiny per-enemy health bars: one bar per linknum group, spanning the group and showing its
// most-damaged part. Shown once an enemy has taken damage (healthbar_seen latch); boss-linked
// groups are skipped; only active, damageable slots qualify.
static void draw_enemy_health_bars(void)
{
	if (!enemyBars)
		return;

	bool done[100] = { false };

	for (unsigned int e = 0; e < 100; e++)
	{
		if (enemyAvail[e] != 0 || done[e])
			continue;

		const int link = enemy[e].linknum;

		// Groups that already own a boss bar are handled by draw_boss_bar().
		if (link != 0 && (link == boss_bar[0].link_num || link == boss_bar[1].link_num))
		{
			done[e] = true;
			continue;
		}

		// Accumulate the group's on-screen bounding box and most-damaged fraction.
		bool shown = false;
		float frac = 1.0f;
		int left = 99999, right = -99999, top = 99999, bottom = -99999;

		for (unsigned int f = e; f < 100; f++)
		{
			// Skip freed (1) and lingering non-damageable (2) slots; this also shrinks
			// a linkgroup's bar to just its surviving parts.
			if (enemyAvail[f] != 0)
				continue;
			if (link == 0 ? (f != e) : (enemy[f].linknum != (JE_byte)link))
				continue;

			done[f] = true;

			// Sprite footprint: a normal enemy is one 12x14 cell at (ex+mapoffset, ey);
			// a "2x2" enemy (size==1) is four cells around that point (+-6 x, +-7 y),
			// i.e. 24x28.
			const bool big = (enemy[f].size == 1);
			const int sx = enemy[f].ex + enemy[f].mapoffset + (big ? -6 : 0);
			const int sy = enemy[f].ey + (big ? -7 : 0);
			const int sw = big ? 24 : 12;
			const int sh = big ? 28 : 14;

			if (sx < left)             left = sx;
			if (sx + sw - 1 > right)   right = sx + sw - 1;
			if (sy < top)              top = sy;
			if (sy + sh > bottom)      bottom = sy + sh;

			// Bar only while alive AND damageable: armorleft == 0 is dead/dying, 255 is the "invincible"
			// sentinel.
			if (enemy[f].healthbar_seen && enemy[f].armorleft > 0 && enemy[f].armorleft < 255 &&
			    enemy[f].healthbar_max >= ENEMY_BAR_MIN_HP)
			{
				shown = true;
				const float f2 = (float)enemy[f].armorleft / (float)enemy[f].healthbar_max;
				if (f2 < frac)
					frac = f2;
			}
		}

		if (shown)
		{
			// Endless special enemies get a bar in their tint bank (elite / champion) so the
			// bar reads as part of the enemy; ordinary enemies keep the bank-7 yellow ramp.
			const Uint8 barTint = endlessEliteTint(enemy[e].eliteState);
			const int barBase = (barTint != 0) ? barTint : 112;  // palette bank 7
			// Slot banks 0/25/50/75 use horizontal anchors 2/1/3/1 respectively (the same
			// batches configured around JE_drawEnemy above). Preserve the representative enemy's
			// absolute anchor so finalize can apply the same draw-order correction to its bar.
			const int par_layer = (e < 25) ? 2 : (e < 50) ? 1 : (e < 75) ? 3 : 1;
			const float par_anchor = (float)(enemy[e].mapoffset - PLAYFIELD_X_SHIFT) + enemy[e].mapoffset_frac;
			draw_enemy_hp_bar(RL_ID_ENEMYBAR_BASE + (int)e, left, right, top, bottom, frac,
			                  barBase, enemy[e].mapoffset_frac, par_layer, par_anchor,
			                  enemy[e].scroll_ybase, enemy[e].scroll_yfrac,
			                  enemy[e].scroll_ylayer);
		}
	}
}

JE_byte boss_bar_fill(unsigned int armorleft, unsigned int full)
{
	if (armorleft >= 255)
		return BOSS_BAR_FULL;  // invincible: a scripted phase shows a full bar
	if (full == 0 || armorleft >= full)
		return BOSS_BAR_FULL;
	return (JE_byte)((armorleft * BOSS_BAR_FULL + full / 2) / full);
}

void boss_bar_survey(JE_byte link_num, unsigned int *out_armor, unsigned int *out_full)
{
	unsigned int armor = 256;  // higher than armor max
	unsigned int full = 0;

	for (unsigned int e = 0; e < COUNTOF(enemy); e++)  // find most damaged
	{
		if (enemyAvail[e] == 0 && enemy[e].linknum == link_num)
			if (enemy[e].armorleft < armor)
			{
				armor = enemy[e].armorleft;
				full = enemy[e].healthbar_max;
			}
	}

	*out_armor = armor;
	*out_full = full;
}

void draw_boss_bar(void)
{
	for (unsigned int b = 0; b < COUNTOF(boss_bar); b++)
	{
		if (boss_bar[b].link_num == 0)
			continue;

		unsigned int armor, full;
		boss_bar_survey(boss_bar[b].link_num, &armor, &full);

		if (armor > 255 || armor == 0)  // boss dead?
		{
			boss_bar[b].link_num = 0;
			// Tally "Bosses slain" here; the one definitive moment a boss that actually
			// had a health bar is destroyed. Each such boss counts exactly once (the bar is
			// skipped once link_num is 0); high-armor mini-bosses that never spawn a bar
			// are never counted.
			if (endlessMode)
				++endlessRunBossKills;
			// SHOCKWAVE boon, top tier: a boss bar emptying wipes the WHOLE field of enemy fire, so the
			// screenful of bullets a dying boss leaves behind can't kill you after the fact.
			if (endlessShockwaveActive())
				endlessShockwaveClear(0, 0, -1);
		}
		else
			boss_bar[b].fill = boss_bar_fill(armor, full);
	}

	unsigned int bars = (boss_bar[0].link_num != 0 ? 1 : 0)
	                  + (boss_bar[1].link_num != 0 ? 1 : 0);

	// if only one bar left, make it the first one
	if (bars == 1 && boss_bar[0].link_num == 0)
	{
		memcpy(&boss_bar[0], &boss_bar[1], sizeof(boss_bar_t));
		boss_bar[1].link_num = 0;
	}

	if (bars == 0)
		return;

	if (bossBarStyle == BOSS_BAR_ENHANCED)
		draw_boss_bars_enhanced(VGAScreen, 1, 1.0f, true, bars);
	else
		draw_boss_bars_classic(bars);
}

// Redraw enhanced boss bars with an interpolated hit flash.
static void draw_boss_bar_present(SDL_Surface *dst, int scale, float alpha)
{
	if (bossBarStyle != BOSS_BAR_ENHANCED)
		return;

	bool flashing = false;
	for (unsigned int b = 0; b < COUNTOF(boss_bar); b++)
	{
		if (boss_bar[b].link_num == 0)
			continue;

		int base, lift;
		boss_bar_colours(boss_bar[b].link_num, &base, &lift);
		if (boss_bar[b].color > 0 || lift > 0)
			flashing = true;
	}
	if (!flashing)
		return;

	const unsigned int bars = (boss_bar[0].link_num != 0 ? 1 : 0)
	                        + (boss_bar[1].link_num != 0 ? 1 : 0);
	if (bars == 0)
		return;

	draw_boss_bars_enhanced(dst, scale, alpha, false, bars);
}

// Shift needed to clear a right-side enhanced boss bar, or 0. Keep this geometry in sync with
// draw_boss_bars_enhanced.
int boss_bar_hud_left_shift(int hudRightX)
{
	const unsigned int bars = (boss_bar[0].link_num != 0 ? 1 : 0)
	                         + (boss_bar[1].link_num != 0 ? 1 : 0);
	if (bars == 0 || bossBarStyle != BOSS_BAR_ENHANCED)
		return 0;
	if (bossBarLayout != BOSS_BAR_LEFT && bossBarLayout != BOSS_BAR_RIGHT)
		return 0;  // horizontal layouts never occupy the right edge column

	const bool splitMode = (bossBarTwoMode == BOSS_BAR_TWO_SPLIT);
	const bool onRight = (bossBarLayout == BOSS_BAR_RIGHT) || (bars == 2 && splitMode);
	if (!onRight)
		return 0;  // vertical bar(s) confined to the left edge

	// Two bars side-by-side ("Together") widen the occupied column; Stacked/Split/single share
	// just one THICK-wide column (see the bx formula in draw_boss_bars_enhanced).
	const bool together = (bars == 2 && !splitMode && bossBarTwoMode == BOSS_BAR_TWO_TOGETHER);
	const int slots = together ? 1 : 0;
	const int leftmostX = (PLAYFIELD_RIGHT - BOSS_BAR_CLEAR) - BOSS_BAR_THICK + 1
	                    - slots * (BOSS_BAR_THICK + BOSS_BAR_GAP);

	return (hudRightX >= leftmostX) ? (hudRightX - leftmostX + 4) : 0;  // +4px clearance
}

// True while an Enhanced BOTTOM horizontal boss bar is shown; it can span near the full
// playfield width and sits close to the bottom edge, unlike TOP which never reaches there.
bool boss_bar_hud_needs_up_shift(void)
{
	const unsigned int bars = (boss_bar[0].link_num != 0 ? 1 : 0)
	                        + (boss_bar[1].link_num != 0 ? 1 : 0);
	return bars > 0 && bossBarStyle == BOSS_BAR_ENHANCED && bossBarLayout == BOSS_BAR_BOTTOM;
}

// Top row of a bottom Enhanced boss bar, or INT_MAX. Keep in step with draw_boss_bars_enhanced.
int boss_bar_bottom_band_top(void)
{
	const unsigned int bars = (boss_bar[0].link_num != 0 ? 1 : 0)
	                        + (boss_bar[1].link_num != 0 ? 1 : 0);
	if (!boss_bar_hud_needs_up_shift())
		return INT_MAX;

	// Two bars stack into rows unless Split puts them side by side on one row.
	const bool stackedRows = (bars == 2 && bossBarTwoMode != BOSS_BAR_TWO_SPLIT);
	const int rows = stackedRows ? 2 : 1;

	const int botAnchor = hud_bottom_band_top() - BOSS_BAR_CLEAR - 1;   // as in the draw
	return botAnchor - (rows * BOSS_BAR_THICK + (rows - 1) * BOSS_BAR_GAP) + 1;
}

/* Rollback state registration.
 * File-local simulation state only. VT input integration, presentation pacing, and render-side
 * scroll mirrors remain outside the rollback boundary. */
#include "rollback.h"

void tyrian2_register_rollback(void)
{
	rollback_register("t2.eventRec",         eventRec, sizeof(eventRec));
	rollback_register("t2.chainPulse",       chainPulse, sizeof(chainPulse));
	rollback_register("t2.chainPulseN",      &chainPulseN, sizeof(chainPulseN));
	rollback_register("t2.chainPulseLast",   &chainPulseLastLink, sizeof(chainPulseLastLink));
	rollback_register("t2.chainPulseOwner",  chainPulseOwner, sizeof(chainPulseOwner));
	rollback_register("t2.chainPulseSalvo",  chainPulseSalvo, sizeof(chainPulseSalvo));
	rollback_register("t2.chainPulseWave",   chainPulseWave, sizeof(chainPulseWave));
	rollback_register("t2.shipTick",         ship_tick_x, sizeof(ship_tick_x));
	rollback_register("t2.shipTickY",        ship_tick_y, sizeof(ship_tick_y));
	/* The latch that arms them. */
	rollback_register("t2.shipPredHave",     &ship_pred_have_tick, sizeof(ship_pred_have_tick));
	rollback_register("t2.tempMapXOfsFrac",  &tempMapXOfs_frac, sizeof(tempMapXOfs_frac));
	rollback_register("t2.tempMapXOfsLayer", &tempMapXOfs_layer, sizeof(tempMapXOfs_layer));
	rollback_register("t2.tempScrollYBase",  &tempScrollYBase, sizeof(tempScrollYBase));
	rollback_register("t2.tempScrollYBaseB", &tempScrollYBaseBar, sizeof(tempScrollYBaseBar));
	rollback_register("t2.tempScrollYfrac",  &tempScrollYfrac, sizeof(tempScrollYfrac));
	rollback_register("t2.tempScrollYfracB", &tempScrollYfracBar, sizeof(tempScrollYfracBar));
	rollback_register("t2.tempScrollYLayer", &tempScrollYLayer, sizeof(tempScrollYLayer));
	rollback_register("t2.tempScrollBase",   &tempScrollBaseStep, sizeof(tempScrollBaseStep));
	rollback_register("t2.tempScrollDelay",  &tempScrollDelayMax, sizeof(tempScrollDelayMax));
	rollback_register("t2.tempScrollExtra",  &tempScrollExtraPx, sizeof(tempScrollExtraPx));
	rollback_register("t2.skyGlue",          &skyGlueThisEnemy, sizeof(skyGlueThisEnemy));
	rollback_register("t2.evScrollValid",    &eventScrollCatchupValid, sizeof(eventScrollCatchupValid));
	rollback_register("t2.evScrollFrom",     &eventScrollFrom, sizeof(eventScrollFrom));
	rollback_register("t2.evScrollTo",       &eventScrollTo, sizeof(eventScrollTo));
	rollback_register("t2.evScrollDelta",    eventScrollLayerDelta, sizeof(eventScrollLayerDelta));
	rollback_register("t2.evScrollBase",     eventScrollBaseStep, sizeof(eventScrollBaseStep));
	rollback_register("t2.evScrollDelayMax", eventScrollDelayMax, sizeof(eventScrollDelayMax));
	rollback_register("t2.evScrollBoost",    &eventScrollBoost, sizeof(eventScrollBoost));
	rollback_register("t2.evSkyValid",       &eventScrollSkyValid, sizeof(eventScrollSkyValid));
	rollback_register("t2.evSkyRatio",       &eventScrollSkyRatio100, sizeof(eventScrollSkyRatio100));
	rollback_register("t2.evSkyPhase",       &eventScrollSkyPhase100, sizeof(eventScrollSkyPhase100));
	rollback_register("t2.bgSmoothRate",     bgSmoothRatePend, sizeof(bgSmoothRatePend));
	rollback_register("t2.bgSmoothFrac",     bgSmoothFracPend, sizeof(bgSmoothFracPend));
	rollback_register("t2.bgSmoothActive",   &bgSmoothActivePend, sizeof(bgSmoothActivePend));
}
