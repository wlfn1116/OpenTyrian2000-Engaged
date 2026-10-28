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
#include "specials.h"

#include "config.h"
#include "debug_flags.h"
#include "demo.h"
#include "endless.h"
#include "episodes.h"
#include "game_state.h"
#include "hud.h"
#include "lvlmast.h"
#include "mouse.h"
#include "mtrand.h"
#include "player.h"
#include "player_ship.h"
#include "rollback.h"
#include "shots.h"
#include "sndmast.h"
#include "sparks.h"
#include "video.h"

#include <stdlib.h>

JE_boolean fireButtonHeld;

/* Player Shot Data */
JE_byte     zinglonDuration;
JE_byte     zinglonRamp;  /* how far the pillar has opened so far, 0..ZINGLON_PILLAR_HALF_W */

/* Per-ship Soul of Zinglon render request. The display pass places the pillar at
 * the render-rate ship position; an explicit flag distinguishes spent duration 1. */
bool        zinglonPillarActive[2] = { false, false };
int         zinglonPillarCX[2] = { 0, 0 };    /* pillar centre x in game_screen coords */
int         zinglonPillarTemp[2] = { 0, 0 };  /* pillar half-width */

JE_byte     astralDuration;
JE_word     flareDuration;
JE_boolean  flareStart;
JE_shortint flareColChg;
/* The rollback-registered flare grade is separate from the level's screen filter. */
bool        flareOwnsFilter = false;
JE_byte     specialWait;
JE_byte     nextSpecialWait;
JE_boolean  spraySpecial;

JE_shortint specialWeaponFilter, specialWeaponFreq;
JE_word     specialWeaponWpn;
JE_boolean  linkToPlayer;

// Give shotless boosted specials a green ship-centered Salvo cue.
static void salvo_special_burst(JE_byte playerNum)
{
	if (!endlessOpeningSalvoVolleyActive())
		return;

	const Player *const p = &player[playerNum - 1];
	JE_doSP(p->x + 7, p->y + 10, 24, 11, ENDLESS_SALVO_SPARK_COLOR, false);
}

/* The pointer that aims this ship's special. Ship two has one of its own only where the ships are
 * independent; the linked pair shares a single arsenal, and with it a single aim. */
static int special_mouse_x_for(JE_byte playerNum) { return dual_ship_mode() && playerNum == 2 ? mouseXB : mouseX; }
static int special_mouse_y_for(JE_byte playerNum) { return dual_ship_mode() && playerNum == 2 ? mouseYB : mouseY; }

/* JE_doSpecialShot clears and reads this flag within one ship's turn. */
static bool specialFiredThisCall = false;

/* The beam widens over its opening ticks, holds while the blast runs, then closes over its last
 * ones. See doc/notes.md#gauges-and-effects. */
int zinglon_pillar_width(int ramp, int duration)
{
	const int closing = duration < ZINGLON_PILLAR_HALF_W ? duration : ZINGLON_PILLAR_HALF_W;
	return ramp < closing ? ramp : closing;
}

// Apply the owner's player-damage scale to the beam.
static int zinglon_pillar_damage(uint owner)
{
	const uint fxSaved = endlessFxPlayer();
	endlessSetFxPlayer(owner);
	const int dmg = endlessScaleOwnDamage(ZINGLON_PILLAR_DAMAGE, endlessOpeningSalvoVolleyActive());
	endlessSetFxPlayer(fxSaved);
	return dmg;
}

bool zinglon_pillar_hit(int hullScreenX, int *width_px, int *damage, uint *owner)
{
	*width_px = 0;
	*damage = 0;
	*owner = 0;

	if (!dual_ship_mode())
	{
		*width_px = zinglon_pillar_width(zinglonRamp, zinglonDuration);
		if (abs(hullScreenX - (player[0].x + 7)) >= *width_px)
			return false;
		*damage = zinglon_pillar_damage(0);
		return true;
	}

	bool hit = false;
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		const int width = zinglon_pillar_width(player[p].zinglon_ramp, player[p].zinglon_duration);
		if (player[p].zinglon_duration <= 1 || abs(hullScreenX - (player[p].x + 7)) >= width)
			continue;

		const int dmg = zinglon_pillar_damage(p);
		if (!hit || dmg > *damage)
		{
			*owner = p;
			*damage = dmg;
		}
		if (width > *width_px)
			*width_px = width;
		hit = true;
	}
	return hit;
}

/* Open the light pillar, or refresh one still running. A blast that lands on a live beam keeps its
 * ramp, so the beam holds the width it reached instead of snapping shut and reopening. */
static void zinglon_blast_start(void)
{
	if (zinglonDuration <= 1)
		zinglonRamp = 0;
	zinglonDuration = (JE_byte)endlessPerkSpecialDuration(50, 255);
}

void JE_specialComplete(JE_byte playerNum, JE_byte specialType)
{
	Player *const this_player = dual_ship_mode() ? &player[playerNum - 1] : &player[0];
	const int special_mouse_x = special_mouse_x_for(playerNum);
	const int special_mouse_y = special_mouse_y_for(playerNum);

	specialFiredThisCall = true;
	nextSpecialWait = 0;
	switch (special[specialType].stype)
	{
		/*Weapon*/
		case 1:
			if (playerNum == 1)
				b = player_shot_create(0, SHOT_SPECIAL2, player[0].x, player[0].y, special_mouse_x, special_mouse_y, special[specialType].wpn, playerNum);
			else
				b = player_shot_create(0, SHOT_SPECIAL2, player[1].x, player[1].y, special_mouse_x, special_mouse_y, special[specialType].wpn, playerNum);

			shotRepeat[SHOT_SPECIAL] = shotRepeat[SHOT_SPECIAL2];
			break;
		/*Repulsor*/
		case 2:
		{
			const int push = endlessOpeningSalvoScale(1);  // Opening Salvo: shoves that much harder
			salvo_special_burst(playerNum);

			// ENEMY_SHOT_MAX exceeds a byte, so this loop needs an int counter.
			for (int es = 0; es < ENEMY_SHOT_MAX; es++)
			{
				if (!enemyShotAvail[es])
				{
					if (this_player->x > enemyShot[es].sx)
						enemyShot[es].sxm -= push;
					else if (this_player->x < enemyShot[es].sx)
						enemyShot[es].sxm += push;

					if (this_player->y > enemyShot[es].sy)
						enemyShot[es].sym -= push;
					else if (this_player->y < enemyShot[es].sy)
						enemyShot[es].sym += push;
				}
			}
			break;
		}
		/*Zinglon Blast*/
		case 3:
			zinglon_blast_start();
			shotRepeat[SHOT_SPECIAL] = 100;
			soundQueue[7] = S_SOUL_OF_ZINGLON;
			break;
		/*Attractor*/
		case 4:
		{
			// Opening Salvo: hauls that much harder. exc/eyc are Sint8 and accumulate per firing,
			// so clamp below; a wrapped speed would fling the pickup the wrong way.
			const int pull = endlessOpeningSalvoScale(1);
			salvo_special_burst(playerNum);
			if (endlessFxActive())
			{
				endlessAttractorFire((uint)(this_player - player), pull);
				break;
			}
			for (temp = 0; temp < 100; temp++)
			{
				if (enemyAvail[temp] != 1 && enemy[temp].scoreitem &&
				    enemy[temp].evalue != 0)
				{
					int exc = enemy[temp].exc, eyc = enemy[temp].eyc;

					if (this_player->x > enemy[temp].ex)
						exc += pull;
					else if (this_player->x < enemy[temp].ex)
						exc -= pull;

					if (this_player->y > enemy[temp].ey)
						eyc += pull;
					else if (this_player->y < enemy[temp].ey)
						eyc -= pull;

					enemy[temp].exc = (JE_shortint)(exc > 120 ? 120 : (exc < -120 ? -120 : exc));
					enemy[temp].eyc = (JE_shortint)(eyc > 120 ? 120 : (eyc < -120 ? -120 : eyc));
				}
			}
			break;
		}
		/*Flare*/
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
		case 16:
			if (flareDuration == 0)
				flareStart = true;

			specialWeaponWpn = special[specialType].wpn;
			linkToPlayer = false;
			spraySpecial = false;
			switch (special[specialType].stype)
			{
				case 5:
					specialWeaponFilter = 7;
					specialWeaponFreq = 2;
					flareDuration = endlessPerkSpecialDuration(50, 0);
					break;
				case 6:
					specialWeaponFilter = 1;
					specialWeaponFreq = 7;
					flareDuration = endlessPerkSpecialDuration(200 + 25 * this_player->items.weapon[FRONT_WEAPON].power, 0);
					break;
				case 7:
					specialWeaponFilter = 3;
					specialWeaponFreq = 3;
					flareDuration = endlessPerkSpecialDuration(50 + 10 * this_player->items.weapon[FRONT_WEAPON].power, 0);
					zinglon_blast_start();
					shotRepeat[SHOT_SPECIAL] = 100;
					soundQueue[7] = S_SOUL_OF_ZINGLON;
					break;
				case 8:
					specialWeaponFilter = -99;
					specialWeaponFreq = 7;
					flareDuration = endlessPerkSpecialDuration(10 + this_player->items.weapon[FRONT_WEAPON].power, 0);
					break;
				case 9:
					specialWeaponFilter = -99;
					specialWeaponFreq = 8;
					flareDuration = endlessPerkSpecialDuration(8 + 2 * this_player->items.weapon[FRONT_WEAPON].power, 0);
					linkToPlayer = true;
					nextSpecialWait = special[specialType].pwr;
					break;
				case 10:
					specialWeaponFilter = -99;
					specialWeaponFreq = 8;
					flareDuration = endlessPerkSpecialDuration(14 + 4 * this_player->items.weapon[FRONT_WEAPON].power, 0);
					linkToPlayer = true;
					break;
				case 11:
					specialWeaponFilter = -99;
					specialWeaponFreq = special[specialType].pwr;
					flareDuration = endlessPerkSpecialDuration(10 + 10 * this_player->items.weapon[FRONT_WEAPON].power, 0);
					astralDuration = endlessPerkSpecialDuration(20 + 10 * this_player->items.weapon[FRONT_WEAPON].power, 255);
					break;
				case 16:
					specialWeaponFilter = -99;
					specialWeaponFreq = 8;
					flareDuration = endlessPerkSpecialDuration(temp2 * 16 + 8, 0);
					linkToPlayer = true;
					spraySpecial = true;
					break;
			}
			break;
		case 12:
			// Opening Salvo x2.5, on top of Ordnance Reserves' stretch. invulnerable_ticks is a uint.
			player[playerNum-1].invulnerable_ticks = endlessOpeningSalvoScale(endlessPerkSpecialDuration(temp2 * 10, 0));
			salvo_special_burst(playerNum);

			// A Super Arcade ship's own invulnerability burst, so it belongs to the firing ship.
			if (player_sa_ship(this_player) != SA_NONE)
			{
				shotRepeat[SHOT_SPECIAL] = 250;
				b = player_shot_create(0, SHOT_SPECIAL2, this_player->x, this_player->y,
				                       special_mouse_x, special_mouse_y, 707, playerNum);
				this_player->invulnerable_ticks = 100;
			}
			break;
		// Opening Salvo scales repair specials. Endless caps them at the ship's
		// reinforced maximum; campaign keeps its stock clamp.
		case 13:
			this_player->armor += endlessOpeningSalvoScale(temp2 / 4 + 1);
			if (endlessFxActive() && this_player->initial_armor > 0 && this_player->armor > this_player->initial_armor)
				this_player->armor = this_player->initial_armor;
			salvo_special_burst(playerNum);

			soundQueue[3] = S_POWERUP;
			break;
		case 14:
		{
			// Repair the partner in co-op, or hull two for the linked pair.
			const JE_byte repair_num = dual_ship_mode() ? (JE_byte)(3 - playerNum) : 2;
			Player *const repair_player = &player[repair_num - 1];
			repair_player->armor += endlessOpeningSalvoScale(temp2 / 4 + 1);
			if (endlessFxActive() && repair_player->initial_armor > 0 && repair_player->armor > repair_player->initial_armor)
				repair_player->armor = repair_player->initial_armor;
			salvo_special_burst(repair_num);

			soundQueue[3] = S_POWERUP;
			break;
		}

		case 17:  // spawn left or right sidekick
			soundQueue[3] = S_POWERUP;

			if (this_player->items.sidekick[LEFT_SIDEKICK] == special[specialType].wpn)
			{
				this_player->items.sidekick[RIGHT_SIDEKICK] = special[specialType].wpn;
				shotMultiPos[RIGHT_SIDEKICK] = 0;
			}
			else
			{
				this_player->items.sidekick[LEFT_SIDEKICK] = special[specialType].wpn;
				shotMultiPos[LEFT_SIDEKICK] = 0;
			}

			if (dual_ship_mode())
			{
				JE_resetPlayerOptions(this_player);
				JE_drawOptionsHUD();
			}
			else
			{
				JE_drawOptions();
			}
			break;

		case 18:  // spawn right sidekick
			this_player->items.sidekick[RIGHT_SIDEKICK] = special[specialType].wpn;

			if (dual_ship_mode())
			{
				JE_resetPlayerOptions(this_player);
				JE_drawOptionsHUD();
			}
			else
			{
				JE_drawOptions();
			}

			soundQueue[4] = S_POWERUP;

			shotMultiPos[RIGHT_SIDEKICK] = 0;
			break;
	}
}

// Flare-type specials hold flareDuration > 0 while active; instant specials don't.
// Used to decide whether the equipped special can fire alongside a twiddle flare.
static bool special_is_flare(JE_byte sidx)
{
	const JE_byte st = special[sidx].stype;
	return (st >= 5 && st <= 11) || st == 16;
}

/* Twiddles have per-ship clocks; the flare fields describe the one live flare. */
static JE_boolean flareFromTwiddle = false;
static JE_word twiddleFlareShotWait = 0;
static JE_word twiddleWait[2] = { 0, 0 };

// A level (or an endless zone) starts with both twiddle clocks idle and no twiddle owning a flare.
void JE_resetTwiddleClocks(void)
{
	flareFromTwiddle = false;
	twiddleFlareShotWait = 0;
	twiddleWait[0] = twiddleWait[1] = 0;
}

/* Every clock that keeps a fired special alive, cleared at level start, the block every mode
 * enters a level through. See doc/notes.md#twiddles-and-specials. */
void JE_resetSpecialState(void)
{
	zinglonDuration = 0;
	zinglonRamp = 0;
	astralDuration = 0;
	flareDuration = 0;
	flareStart = false;
	specialWait = 0;
	nextSpecialWait = 0;
	debugTwiddleTrigger = false;  // an unconsumed one-shot must not fire into the next level
	JE_resetTwiddleClocks();
}

/* Twiddles use their own recharge clock; flare twiddles are paced by the live flare. */
static void twiddle_fire(JE_byte playerNum, uint slot, JE_byte specialType)
{
	const JE_byte equipped_wait = shotRepeat[SHOT_SPECIAL];
	shotRepeat[SHOT_SPECIAL] = 0;

	JE_specialComplete(playerNum, specialType);

	if (special_is_flare(specialType))
	{
		flareFromTwiddle = true;
		twiddleFlareShotWait = 0;
		twiddleWait[slot] = 0;
	}
	else
	{
		twiddleWait[slot] = MAX(shotRepeat[SHOT_SPECIAL], TWIDDLE_MIN_WAIT);
	}

	shotRepeat[SHOT_SPECIAL] = equipped_wait;
}

void JE_doSpecialShot(JE_byte playerNum, uint *armor, uint *shield)
{
	Player *const this_player = dual_ship_mode() ? &player[playerNum - 1] : &player[0];
	const int special_mouse_x = special_mouse_x_for(playerNum);
	const int special_mouse_y = special_mouse_y_for(playerNum);

	const uint special_player = (uint)(this_player - player);
	const uint twiddle_slot = (playerNum >= 2) ? 1u : 0u;
	specialFiredThisCall = false;

	if (shotRepeat[SHOT_SPECIAL] > 0)
	{
		--shotRepeat[SHOT_SPECIAL];
	}
	if (specialWait > 0)
	{
		specialWait--;
	}
	if (twiddleWait[twiddle_slot] > 0)
	{
		--twiddleWait[twiddle_slot];
	}

	// Sample readiness before either fire gate can spend it. The HUD uses this sample to show a
	// special that becomes ready and fires within the same tick; flareDuration must match the gate.
	const bool special_armed = shotRepeat[SHOT_SPECIAL] == 0 && specialWait == 0
	                        && flareDuration == 0 && zinglonDuration < 2;

	/* JE_playerMovement clears a recognized twiddle at the start of the next tick. */
	const JE_byte twiddle_special = SFExecuted[playerNum-1];
	if (twiddle_special > 0 && twiddleWait[twiddle_slot] == 0
	    && (flareDuration == 0 || !special_is_flare(twiddle_special)))
	{
		temp2 = special[twiddle_special].pwr;

		bool can_afford = true;
		uint spend_shield = 0, spend_armor = 0;

		/* Kinetic Converter discounts the charge without changing the effect magnitude in temp2. */
		if (temp2 > 0)
		{
			if (temp2 < 98)  // costs some shield
			{
				spend_shield = (uint)endlessPerkKineticTwiddleCost(temp2);
				can_afford = *shield >= spend_shield;
			}
			else if (temp2 == 98)  // costs all shield
			{
				can_afford = *shield >= 4;
				spend_shield = (uint)endlessPerkKineticTwiddleCost((int)*shield);
				temp2 = *shield;
			}
			else if (temp2 == 99)  // costs half shield
			{
				const uint stock_spend = *shield - *shield / 2;  // vanilla keeps the smaller half
				spend_shield = (uint)endlessPerkKineticTwiddleCost((int)stock_spend);
				temp2 = *shield / 2;
			}
			else  // costs some armor
			{
				temp2 -= 100;
				spend_armor = (uint)endlessPerkKineticTwiddleCost(temp2);
				can_afford = *armor > spend_armor;
			}
		}

		shotMultiPos[SHOT_SPECIAL] = 0;
		shotMultiPos[SHOT_SPECIAL2] = 0;

		if (can_afford)
		{
			*shield -= spend_shield;
			*armor -= spend_armor;
			twiddle_fire(playerNum, twiddle_slot, twiddle_special);
		}

		SFExecuted[playerNum-1] = 0;

		JE_wipeShieldArmorBars();
		VGAScreen = VGAScreenSeg; /* side-effect of game_screen */
		JE_drawShield();
		JE_drawArmor();
		VGAScreen = game_screen; /* side-effect of game_screen */
	}

	if ((playerNum == 1 || dual_ship_mode()) && this_player->items.special > 0)
	{  /*Main Begin*/

		if (superArcadeMode > 0 && (button[2-1] || button[3-1]))
		{
			fireButtonHeld = false;
		}
		if (!button[1-1] && !(superArcadeMode != SA_NONE && (button[2-1] || button[3-1])))
		{
			fireButtonHeld = false;
		}
		// A live Zinglon beam turns a refire away as a live flare does, the clause the special_armed
		// sample above already carried. Only a recharge Rapid Recharge drained early reaches it.
		else if (shotRepeat[SHOT_SPECIAL] == 0 && !fireButtonHeld && zinglonDuration < 2 &&
		         (flareDuration == 0 || (flareFromTwiddle && !special_is_flare(this_player->items.special))) &&
		         specialWait == 0)
		{
			fireButtonHeld = true;
			JE_specialComplete(playerNum, this_player->items.special);
		}
		else if (endlessMode)
		{
			// Endless latches held input during recharge so it cannot fire on the ready
			// edge. Autofire Special bypasses this latch below.
			fireButtonHeld = true;
		}

	}  /*Main End*/

	// A demo replays a recorded input stream, so the debug fire helpers stay out of it: the held
	// fire button they read comes from the recording rather than from a player at the controls.
	const bool debug_fire_helpers = !play_demo && !record_demo;

	if (((autoFireSpecial && debug_fire_helpers) || endlessPerkAutoFireSpecial()) &&
		(playerNum == 1 || dual_ship_mode()) && this_player->items.special > 0 &&
		shotRepeat[SHOT_SPECIAL] == 0 && specialWait == 0 && zinglonDuration < 2 &&
		(flareDuration == 0 || (flareFromTwiddle && !special_is_flare(this_player->items.special))) &&
		(button[0] || (superArcadeMode != SA_NONE && (button[1] || button[2]))))
	{
		JE_specialComplete(playerNum, this_player->items.special);
	}

	// Debug twiddles use the normal twiddle clock after the equipped special gets priority.
	if (playerNum == 1 && debug_fire_helpers)
	{
		const bool want = debugTwiddleTrigger
		               || (debugAutofireTwiddle && button[0] && twiddleWait[0] == 0);

		if (want && debugTwiddleSpecial >= 1 && debugTwiddleSpecial <= SPECIAL_NUM &&
		    flareDuration == 0)
		{
			debugTwiddleTrigger = false;

			// JE_specialComplete reads global temp2 for its duration/effect maths;
			// seed it like the cost path but without deducting, so it fires free.
			temp2 = special[debugTwiddleSpecial].pwr;
			if (temp2 == 98)        temp2 = *shield;
			else if (temp2 == 99)   temp2 = *shield / 2;
			else if (temp2 >= 100)  temp2 -= 100;
			shotMultiPos[SHOT_SPECIAL] = 0;
			shotMultiPos[SHOT_SPECIAL2] = 0;

			twiddle_fire(playerNum, 0, debugTwiddleSpecial);
		}
	}

	if (astralDuration > 0)
		astralDuration--;

	shotAvail[MAX_PWEAPON-1] = 0;
	if (flareDuration > 1)
	{
		if (specialWeaponFilter != -99)
		{
			if (levelFilter == -99 && levelBrightness == -99)
			{
				filterActive = false;
			}
			if (!filterActive)
			{
				levelFilter = specialWeaponFilter;
				if (levelFilter == 7)
				{
					levelBrightness = 0;
				}
				filterActive = true;
				flareOwnsFilter = true;
			}

			if (mt_rand() % 2 == 0)
				flareColChg = -1;
			else
				flareColChg = 1;

			// Pulse only a grade this flare installed; a level's own filter 7 keeps its brightness.
			if (flareOwnsFilter && levelFilter == 7)
			{
				if (levelBrightness < -6)
				{
					flareColChg = 1;
				}
				if (levelBrightness > 6)
				{
					flareColChg = -1;
				}
				levelBrightness += flareColChg;
			}
		}

		if (flareFromTwiddle && twiddleFlareShotWait > 0)
			--twiddleFlareShotWait;

		if ((signed)(mt_rand() % 6) < specialWeaponFreq)
		{
			b = MAX_PWEAPON;

			if (linkToPlayer)
			{
				if (flareFromTwiddle)
				{
					// Twiddle flare: pace mines on twiddleFlareShotWait, keeping the
					// equipped special's shotRepeat[SHOT_SPECIAL] untouched.
					if (twiddleFlareShotWait == 0)
					{
						const int savedSR = shotRepeat[SHOT_SPECIAL];
						b = player_shot_create(0, SHOT_SPECIAL, this_player->x, this_player->y, special_mouse_x, special_mouse_y, specialWeaponWpn, playerNum);
						twiddleFlareShotWait = shotRepeat[SHOT_SPECIAL];  // capture the mine cadence
						shotRepeat[SHOT_SPECIAL] = savedSR;
					}
				}
				else if (shotRepeat[SHOT_SPECIAL] == 0)
				{
					b = player_shot_create(0, SHOT_SPECIAL, this_player->x, this_player->y, special_mouse_x, special_mouse_y, specialWeaponWpn, playerNum);
				}
			}
			else
			{
				// Scatter across the full widescreen playfield. Sequence RNG draws so compilers cannot
				// assign them to opposite coordinates.
				const int scatter_x = PLAYFIELD_LEFT + mt_rand() % PLAYFIELD_WIDTH;
				const int scatter_y = mt_rand() % 184;
				b = player_shot_create(0, SHOT_SPECIAL, scatter_x, scatter_y, special_mouse_x, special_mouse_y, specialWeaponWpn, playerNum);
			}

			if (spraySpecial && b != MAX_PWEAPON)
			{
				playerShotData[b].shotXM = (mt_rand() % 5) - 2;
				playerShotData[b].shotYM = (mt_rand() % 5) - 2;
				if (playerShotData[b].shotYM == 0)
				{
					playerShotData[b].shotYM++;
				}
			}
		}

		flareDuration--;
		if (flareDuration == 1)
		{
			specialWait = nextSpecialWait;
		}
	}
	else if (flareStart)
	{
		flareStart = false;
		// The flare's tail charges whichever clock started it.
		const JE_byte flare_wait = linkToPlayer ? 15 : 200;
		if (flareFromTwiddle)
			twiddleWait[twiddle_slot] = flare_wait;
		else
			shotRepeat[SHOT_SPECIAL] = flare_wait;
		flareDuration = 0;
		flareFromTwiddle = false;
		twiddleFlareShotWait = 0;
		// Release only tinted flare-owned grades; see doc/notes.md#twiddles-and-specials.
		if (flareOwnsFilter && specialWeaponFilter != -99)
		{
			levelFilter = -99;
			levelBrightness = -99;
			filterActive = false;
			flareOwnsFilter = false;
		}
	}

	if (zinglonDuration > 1)
	{
		temp = (JE_byte)zinglon_pillar_width(zinglonRamp, zinglonDuration);

		// Record the pillar for the render layer (JE_starShowVGA) instead of drawing:
		// into game_screen it would snap at 35Hz and freeze the scrolled background.
		zinglonPillarActive[special_player] = true;
		zinglonPillarCX[special_player] = this_player->x + 7;
		zinglonPillarTemp[special_player] = temp;

		// Opening Salvo: the pillar is a brightness effect, not a sprite, so it has no colour to
		// trail. Scatter sparks up the beam in the salvo's green, width following its own ramp.
		if (endlessOpeningSalvoVolleyActive() && temp > 0)
			JE_doSP(this_player->x + 7, mt_rand() % 184, 6, (JE_byte)temp, ENDLESS_SALVO_SPARK_COLOR, false);

		if (zinglonRamp < ZINGLON_PILLAR_HALF_W)
			zinglonRamp++;
		zinglonDuration--;
		if (zinglonDuration % 5 == 0)
		{
			shotAvail[MAX_PWEAPON-1] = 1;
		}
	}

	// Publish the displayed ship's clocks after all fire and duration updates. `flareStart` keeps
	// the final burn tick connected to the recharge that it installs.
	if (hud_special_block_shown(special_player))
		hud_special_light_publish(MAX(shotRepeat[SHOT_SPECIAL], specialWait),
		                          MAX(flareStart ? (int)flareDuration : 0,
		                              zinglonDuration > 1 ? (int)zinglonDuration : 0),
		                          special_armed, specialFiredThisCall);
}

/* Register this file's private simulation state. Public globals are registered
 * centrally in rollback_state.c. */
void specials_register_rollback(void)
{
	rollback_register("vz.flareFromTwiddle",    &flareFromTwiddle, sizeof(flareFromTwiddle));
	rollback_register("vz.twiddleFlareWait",    &twiddleFlareShotWait, sizeof(twiddleFlareShotWait));
	rollback_register("vz.twiddleWait",         twiddleWait, sizeof(twiddleWait));
	rollback_register("vz.flareOwnsFilter",     &flareOwnsFilter, sizeof(flareOwnsFilter));
}
