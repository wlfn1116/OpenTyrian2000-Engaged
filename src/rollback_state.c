/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Central registration for externally visible simulation state. File-local statics register in
 * their owning files. Live input, variable-timestep integration, presentation state, cosmetic
 * sparks, and Endless effects remain outside the rollback boundary.
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
#include "rollback.h"

#include "backgrnd.h"
#include "config.h"
#include "episodes.h"
#include "fonthand.h"
#include "mainint.h"
#include "mouse.h"
#include "mtrand.h"
#include "musmast.h"
#include "nortsong.h"
#include "opentyr.h"
#include "params.h"
#include "player.h"
#include "shots.h"
#include "sprite.h"
#include "tyrian2.h"
#include "varz.h"

void varz_register_rollback(void);
void backgrnd_register_rollback(void);
void endless_combat_register_rollback(void);
void endless_register_rollback(void);

/* Rebind player[].lives after restore instead of trusting its copied interior
 * pointer. player_lives_port preserves the original weapon-bay ownership. */
static void rb_fixup_player_lives(void)
{
	for (uint i = 0; i < COUNTOF(player); ++i)
		player[i].lives = &player[i].items.weapon[player_lives_port(i)].power;
}

#define REG(var)       rollback_register(#var, &(var), sizeof(var))
#define REG_ARR(var)   rollback_register(#var, (var), sizeof(var))

void rollback_state_register_globals(void)
{
	/* RNG determinism anchor. */
	rollback_register_callback("mtrand", mt_state_size(), mt_state_save, mt_state_restore);

	/* Players. */
	REG_ARR(player);
	rollback_register_fixup(rb_fixup_player_lives);
	REG_ARR(button);
	REG(constantLastX);
	REG(mouseX);  REG(mouseY);  REG(mouseXB);  REG(mouseYB);
	REG(twoPlayerLinked);
	REG(linkGunDirec);
	/* Snapshot the ship-graphic cache because JE_getShipInfo has gameplay side
	 * effects and cannot rebuild it after restore. */
	REG(shipGr);   REG(shipGrPtr);
	REG(shipGr2);  REG(shipGr2ptr);
	REG(twoPlayerMode);        /* galaga mode clears this mid-level         */
	REG(coopCampaignMode);
	REG(coopEndlessMode);
	REG(galagaMode);
	REG(galagaShotFreq);
	REG(galagaLife);
	REG(superArcadePowerUp);

	/* Player shots. */
	REG_ARR(playerShotData);
	REG_ARR(shotAvail);
	REG_ARR(shotRepeat);
	REG_ARR(shotMultiPos);
	REG(portConfigChange);
	REG(portConfigDone);

	/* Enemies. */
	REG_ARR(enemy);
	REG_ARR(enemyAvail);
	REG_ARR(enemySpriteSheetIds);
	REG(enemyOffset);
	REG(enemyOnScreen);
	REG(enemyParkedAbove);
	REG(mapStopStallTicks);
	REG(superEnemy254Jump);
	REG(totalEnemy);
	REG(enemyKilled);
	REG(enemyStillExploding);
	/* Level events 49-52 rewrite enemy template slot 0 mid-level. */
	rollback_register("enemyDat[0]", &enemyDat[0], sizeof(enemyDat[0]));
	/* Secret-orb warp latch (episodes.c).  The pickup is guarded by !bonusLevel;
	 * left out of the registry, a re-simulated pickup found the flag already set
	 * from the first pass and skipped the nextLevel warp assignment. */
	REG(bonusLevel);

	/* Enemy shots. */
	REG_ARR(enemyShot);
	REG_ARR(enemyShotAvail);

	/* Explosions consume slots deterministically. */
	REG_ARR(explosions);
	REG_ARR(rep_explosions);
	REG(explosionFollowAmountX);
	REG(explosionFollowAmountY);

	/* Street Fighter twiddle detector. */
	REG_ARR(SFCurrentCode);
	REG_ARR(SFExecuted);

	/* Specials, charge weapons, and sidekicks. */
	REG(zinglonDuration);
	REG(zinglonRamp);
	REG_ARR(zinglonPillarActive);
	REG_ARR(zinglonPillarCX);
	REG_ARR(zinglonPillarTemp);
	REG(astralDuration);
	REG(flareDuration);
	REG(flareStart);
	REG(flareColChg);
	REG(specialWait);
	REG(nextSpecialWait);
	REG(spraySpecial);
	REG(doIced);
	REG(infiniteShot);
	REG(specialWeaponFilter);
	REG(specialWeaponFreq);
	REG(specialWeaponWpn);
	REG(linkToPlayer);
	REG(fireButtonHeld);
	REG(debugTwiddleTrigger);      /* one-shot, cleared inside the tick      */
	REG(debugToggleFireActive);    /* latch that overwrites button[0]        */
	REG(chargeWait);  REG(chargeLevel);  REG(chargeMax);
	REG(chargeGr);    REG(chargeGrWait);
	REG(neat);
	REG(optionSatelliteRotate);
	REG_ARR(optionAttachmentMove);
	REG_ARR(optionAttachmentLinked);
	REG_ARR(optionAttachmentReturn);

	/* Level flow and events. */
	REG(curLoc);
	REG(eventLoc);
	REG(maxEvent);
	REG(levelEnemyFrequency);
	REG(tempBackMove);
	REG(explodeMove);
	REG(levelEnd);
	REG(levelEndFxWait);
	REG(levelEndWarp);
	REG(endLevel);
	REG(reallyEndLevel);
	REG(waitToEndLevel);
	REG(playerEndLevel);
	REG(normalBonusLevelCurrent);
	REG(bonusLevelCurrent);
	REG(smallEnemyAdjust);
	REG(readyToEndLevel);
	REG(quitRequested);
	REG_ARR(newPL);
	REG(returnLoc);
	REG(returnActive);
	REG(firstGameOver);
	REG(gameLoaded);
	REG(flash);
	REG(flashChange);
	REG(displayTime);
	REG_ARR(soundQueue);
	REG(enemyContinualDamage);
	REG(enemiesActive);
	REG(forceEvents);
	REG(stopBackgrounds);
	REG(stopBackgroundNum);
	REG(damageRate);
	REG(background3x1);
	REG(background3x1b);
	REG(levelTimer);
	REG(levelTimerCountdown);
	REG(levelTimerJumpTo);
	REG(randomExplosions);
	REG(editShip1);
	REG(editShip2);
	REG_ARR(globalFlags);
	REG(levelSong);
	REG(allPlayersGone);
	REG(boss_bar);
	REG(youAreCheating);
	REG(difficultyLevel);
	REG(oldDifficultyLevel);
	REG(nextLevel);
	REG(saveLevel);
	REG(cubeMax);
	REG_ARR(cubeList);
	REG(timedBattleMode);

	/* Filters and draw-order flags set by level events. */
	REG(levelFilter);
	REG(levelFilterNew);
	REG(levelBrightness);
	REG(levelBrightnessChg);
	REG(filterActive);
	REG(filterFade);
	REG(filterFadeStart);
	REG(background2over);
	REG(background3over);
	REG(background2);
	REG(starActive);
	REG(topEnemyOver);
	REG(skyEnemyOverAll);
	REG(background2notTransparent);

	/* HUD-adjacent counters read by simulation. */
	REG(power);
	REG(lastPower);
	REG(shieldWait);
	// Gauge flashes are presentation state and do not belong in rollback snapshots.
	REG(armorShipDelay);
	REG(warningSoundDelay);
	REG(warningCol);
	REG(warningColChange);
	REG(musicFade);
	REG(tempVolume);
	REG(frameCountMax);   /* pentiumMode toggles it 2<->3 inside the tick */

	/* Background scroll state. */
	REG(backPos);   REG(backPos2);   REG(backPos3);
	REG(backMove);  REG(backMove2);  REG(backMove3);
	REG(mapX);  REG(mapY);  REG(mapX2);  REG(mapY2);  REG(mapX3);  REG(mapY3);
	/* Pointers into the fixed megaData globals: raw values restore correctly
	 * within one process, which is the only place a snapshot ever lives. */
	REG(mapYPos);  REG(mapY2Pos);  REG(mapY3Pos);
	REG(mapXPos);  REG(oldMapXOfs);  REG(mapXOfs);  REG(mapX2Ofs);
	REG(mapX2Pos);  REG(mapX3Pos);  REG(oldMapX3Ofs);  REG(mapX3Ofs);
	REG(tempMapXOfs);
	REG(mapXbpPos);  REG(mapX2bpPos);  REG(mapX3bpPos);
	REG(map1YDelay);  REG(map1YDelayMax);
	REG(map2YDelay);  REG(map2YDelayMax);
	REG_ARR(smoothie_data);
	REG(starfield_speed);
	/* Script events toggle these mid-level, and smoothies[8] inverts the vertical control axis
	 * and mirrors twiddle direction, so a replayed tick must start them from the same point or
	 * inputs read differently. */
	REG_ARR(smoothies);
	REG(starShowVGASpecialCode);
	REG(endlessScrollExtraPx1);
	REG(endlessScrollExtraPx2);
	REG(endlessScrollExtraPx3);
	/* The smooth-scroll float mirrors are presentation-facing, but their values
	 * are stamped into enemy[] (mapoffset_frac / scroll_yfrac) during the draw
	 * pass. A replayed tick must start them from the same point or the
	 * stamped bytes differ (the self-test caught exactly this). */
	REG(mapXOfs_f);  REG(mapX2Ofs_f);  REG(mapX3Ofs_f);
	REG(oldMapXOfs_f);  REG(oldMapX3Ofs_f);
	REG_ARR(bgScrollDeltaY);
	REG(bgMarginRows);
	REG_ARR(bg_layer_dx);   REG_ARR(bg_layer_frac);
	REG_ARR(bg_layer_xofs); REG_ARR(bg_layer_xofs_valid);
	REG_ARR(bg_layer_dy);   REG_ARR(bg_layer_yfrac);
	REG(bg_smooth_y_active);
	REG_ARR(bg_layer_yfrac_now);  REG_ARR(bg_layer_dy_now);
	REG(background_advance);
	REG(anySmoothies);
	REG(BKwrap1);  REG(BKwrap2);  REG(BKwrap3);
	REG(BKwrap1to);  REG(BKwrap2to);  REG(BKwrap3to);

	/* Shared scratch retained for deterministic snapshots. */
	REG(temp);  REG(temp2);  REG(temp3);
	REG(tempW);
	REG(tempDat);  REG(tempDat2);  REG(tempDat3);
	REG(x);  REG(y);  REG(b);

	/* File-local statics registered by their owners. */
	varz_register_rollback();
	backgrnd_register_rollback();
	endless_combat_register_rollback();
	endless_register_rollback();
}
