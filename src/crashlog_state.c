/*
 * Crash-handler state snapshot. Keep this file free of windows.h and avoid
 * allocation, locks, and untrusted pointers.
 */
#include "config.h"
#include "crashlog.h"
#include "custom_weapon.h"
#include "endless.h"
#include "episodes.h"
#include "loudness.h"
#include "musmast.h"
#include "nortsong.h"
#include "player.h"
#include "render_list.h"
#include "shots.h"
#include "tyrian2.h"
#include "varz.h"
#include "video.h"
#include "video_scale.h"

#include <stdio.h>

// Declared locally (matches network.h) to avoid pulling SDL_net into this TU.
extern bool isNetworkGame;
void network_write_diagnostics(FILE *f);

// Volatile phase breadcrumb read by fault handlers on other threads.
static const char *volatile g_phase = "startup";
void crashlog_set_phase(const char *phase) { if (phase != NULL) g_phase = phase; }
const char *crashlog_get_phase(void) { return g_phase; }

// Portable watchdog setting consumed by the Windows watchdog thread.
static int g_hangTimeout = CRASHLOG_HANG_TIMEOUT_DEFAULT;
void crashlog_set_hang_timeout(int seconds)
{
	if (seconds < CRASHLOG_HANG_TIMEOUT_MIN) seconds = CRASHLOG_HANG_TIMEOUT_MIN;
	if (seconds > CRASHLOG_HANG_TIMEOUT_MAX) seconds = CRASHLOG_HANG_TIMEOUT_MAX;
	g_hangTimeout = seconds;
}
int crashlog_get_hang_timeout(void) { return g_hangTimeout; }

// Fault-safe item names use "?" before data load or for invalid ids.
static const char *trim_name(const char *s)
{
	static char buf[4][32];
	static unsigned slot = 0;
	char *out = buf[slot++ & 3u];

	if (s == NULL)
		return "?";

	size_t n = 0;
	while (n < 31 && s[n] != '\0') { out[n] = s[n]; ++n; }
	while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) --n;
	out[n] = '\0';
	return out;
}

static const char *ship_name   (int id) { return trim_name((id >= 0 && id <= SHIP_DRAGONWING && ships[id].name[0])  ? ships[id].name      : "?"); }
static const char *port_name   (int id) { return trim_name((id >= 0 && id <= PORT_NUM    && weaponPort[id].name[0]) ? weaponPort[id].name : "?"); }
static const char *power_name  (int id) { return trim_name((id >= 0 && id <= POWER_NUM   && powerSys[id].name[0])   ? powerSys[id].name   : "?"); }
static const char *shield_name (int id) { return trim_name((id >= 0 && id <= SHIELD_NUM  && shields[id].name[0])    ? shields[id].name    : "?"); }
static const char *option_name (int id) { return trim_name((id >= 0 && id <= OPTION_NUM  && options[id].name[0])    ? options[id].name    : "?"); }
static const char *special_name(int id) { return trim_name((id >= 0 && id <= SPECIAL_NUM && special[id].name[0])    ? special[id].name    : "?"); }

// On-screen object-pool census.
typedef struct
{
	int enemiesActive, enemiesShootable;
	int playerShots, enemyShots;
	int explosions, repExplosions, sparks;
} PoolCensus;

static void take_pool_census(PoolCensus *c)
{
	c->enemiesActive = c->enemiesShootable = 0;
	for (int i = 0; i < 100; ++i)
	{
		if (enemyAvail[i] != 1) ++c->enemiesActive;
		if (enemyAvail[i] == 0) ++c->enemiesShootable;
	}

	c->playerShots = 0;
	for (int i = 0; i < MAX_PWEAPON; ++i)
		if (shotAvail[i] != 0) ++c->playerShots;

	c->enemyShots = 0;
	for (int i = 0; i < ENEMY_SHOT_MAX; ++i)
		if (enemyShotAvail[i] == 0) ++c->enemyShots;

	c->explosions = 0;
	for (int i = 0; i < MAX_EXPLOSIONS; ++i)
		if (explosions[i].ttl != 0) ++c->explosions;

	c->repExplosions = 0;
	for (int i = 0; i < MAX_REPEATING_EXPLOSIONS; ++i)
		if (rep_explosions[i].ttl != 0) ++c->repExplosions;

	c->sparks = 0;
	for (unsigned i = 0; i < MAX_SUPERPIXELS; ++i)
		if (superpixels[i].z != 0) ++c->sparks;
}

// Best-effort single-word label for the active game mode (the raw flags follow it, so an
// imperfect label is never ambiguous).
static const char *mode_label(void)
{
	if (endlessMode)                        return "Endless";
	if (superArcadeMode == SA_ARCADE)       return "Arcade";
	if (superTyrian ||
	    superArcadeMode == SA_SUPERTYRIAN)  return "SuperTyrian";
	if (timedBattleMode)                    return "Timed Battle";
	if (galagaMode)                         return "Galaga";
	if (twoPlayerMode)                      return "2 Player";
	if (onePlayerAction)                    return "1P Arcade";
	return "Campaign";
}

static void write_player(FILE *f, int p)
{
	const Player      *pl = &player[p];
	const PlayerItems *it = &pl->items;

	fprintf(f, "\nPlayer %d%s:\n", p + 1, pl->is_dragonwing ? " (Dragonwing)" : "");

	fprintf(f, "  Alive:        %s", pl->is_alive ? "yes" : "no");
	if (pl->invulnerable_ticks) fprintf(f, "  invuln=%u", pl->invulnerable_ticks);
	if (pl->exploding_ticks)    fprintf(f, "  exploding=%u", pl->exploding_ticks);
	fprintf(f, "\n");

	fprintf(f, "  Ship:         %-3d %s\n", (int)it->ship, ship_name(it->ship));
	fprintf(f, "  Generator:    %-3d %s\n", (int)it->generator, power_name(it->generator));
	fprintf(f, "  Shield:       %-3d %s  (%u/%u)\n", (int)it->shield, shield_name(it->shield), pl->shield, pl->shield_max);
	fprintf(f, "  Armor:        %u/%u  (hull %u)\n", pl->armor, pl->initial_armor, pl->hull_armor);
	fprintf(f, "  Front weapon: %-3d %s\n", (int)it->weapon[FRONT_WEAPON].id, port_name(it->weapon[FRONT_WEAPON].id));
	fprintf(f, "  Front weapon power: %d\n", (int)it->weapon[FRONT_WEAPON].power);
	fprintf(f, "  Rear weapon:  %-3d %s\n", (int)it->weapon[REAR_WEAPON].id, port_name(it->weapon[REAR_WEAPON].id));
	fprintf(f, "  Rear weapon power: %d\n", (int)it->weapon[REAR_WEAPON].power);
	fprintf(f, "  Weapon mode: %u\n", pl->weapon_mode);
	fprintf(f, "  Sidekick L: %-3d %s\n", (int)it->sidekick[LEFT_SIDEKICK], option_name(it->sidekick[LEFT_SIDEKICK]));
	fprintf(f, "  Sidekick L state: charge=%u (ticks %u)  ammo=%d/%d\n",
	        pl->sidekick[LEFT_SIDEKICK].charge, pl->sidekick[LEFT_SIDEKICK].charge_ticks,
	        pl->sidekick[LEFT_SIDEKICK].ammo, pl->sidekick[LEFT_SIDEKICK].ammo_max);
	fprintf(f, "  Sidekick R: %-3d %s\n", (int)it->sidekick[RIGHT_SIDEKICK], option_name(it->sidekick[RIGHT_SIDEKICK]));
	fprintf(f, "  Sidekick R state: charge=%u (ticks %u)  ammo=%d/%d\n",
	        pl->sidekick[RIGHT_SIDEKICK].charge, pl->sidekick[RIGHT_SIDEKICK].charge_ticks,
	        pl->sidekick[RIGHT_SIDEKICK].ammo, pl->sidekick[RIGHT_SIDEKICK].ammo_max);
	fprintf(f, "  Special:      %-3d %s\n", (int)it->special, special_name(it->special));
	fprintf(f, "  Cash:         %lld\n", (long long)pl->cash);
	fprintf(f, "  Superbombs:   %u\n", pl->superbombs);
	fprintf(f, "  Position:     (%d, %d)\n", pl->x, pl->y);
	if (it->super_arcade_mode)
		fprintf(f, "  SuperArcade:  %d\n", (int)it->super_arcade_mode);
}

// Decode Endless modifiers locally and report unknown bits.
static void write_endless_mods(FILE *f, Uint64 mods)
{
	static const struct { Uint64 bit; const char *name; } tbl[] = {
		{ ENDLESS_MOD_FORTIFIED,   "Fortified"   }, { ENDLESS_MOD_FRENZY,     "Frenzy"     },
		{ ENDLESS_MOD_SWIFT,       "Swift"       }, { ENDLESS_MOD_FRAGILE,    "Fragile"    },
		{ ENDLESS_MOD_BOUNTY,      "Bounty"      }, { ENDLESS_MOD_DEVASTATING,"Devastating"},
		{ ENDLESS_MOD_ELITEPACK,   "ElitePack"   }, { ENDLESS_MOD_APEX,       "Apex"       },
		{ ENDLESS_MOD_LEGION,      "Legion"      }, { ENDLESS_MOD_ENRAGE,     "Enrage"     },
		{ ENDLESS_MOD_KAMIKAZE,    "Kamikaze"    }, { ENDLESS_MOD_GRAVITY,    "Gravity"    },
		{ ENDLESS_MOD_TURBODRIVE,  "Turbodrive"  }, { ENDLESS_MOD_OVERCHARGE, "Overcharge" },
		{ ENDLESS_MOD_DILATION,    "Dilation"    }, { ENDLESS_MOD_FAVOR,      "Favor"      },
		{ ENDLESS_MOD_CURSED,      "Cursed"      }, { ENDLESS_MOD_OVERCLOCK,  "Overclock"  },
		{ ENDLESS_MOD_SLIPSTREAM,  "Slipstream"  }, { ENDLESS_MOD_OVERLOAD,   "Overload"   },
		{ ENDLESS_MOD_WARP,        "Warp"        }, { ENDLESS_MOD_OVERDRIVE,  "Overdrive"  },
		{ ENDLESS_MOD_BACKFIRE,    "Backfire"    }, { ENDLESS_MOD_BURNOUT,    "Burnout"    },
		{ ENDLESS_MOD_OVERBLAST,   "Overblast"   }, { ENDLESS_MOD_MISFIRE,    "Misfire"    },
		{ ENDLESS_MOD_MARKED,      "Marked"      }, { ENDLESS_MOD_NITRO,      "Nitro"      },
		{ ENDLESS_MOD_OVERHEAT,    "Overheat"    }, { ENDLESS_MOD_DUD,        "Dud"        },
		{ ENDLESS_MOD_HOMING,      "Homing"      }, { ENDLESS_MOD_RAMPAGE,    "Rampage"    },
		{ ENDLESS_MOD_TOPSY,       "Topsy"       }, { ENDLESS_MOD_SLUGGISH,   "Sluggish"   },
		{ ENDLESS_MOD_SHIELDLESS,  "Shieldless"  }, { ENDLESS_MOD_DEADGEN,    "DeadGen"    },
		{ ENDLESS_MOD_GRAVITY_OMNI,"GravityOmni" },
		{ ENDLESS_MOD_NOCHAMP,     "NoChamp"     }, { ENDLESS_MOD_NOELITE,    "NoElite"    },
		{ ENDLESS_MOD_THEEND,      "TheEnd"      },
		{ ENDLESS_MOD_MARTYRDOM,   "Martyrdom"   }, { ENDLESS_MOD_SEEKER,     "Seeker"     },
		{ ENDLESS_MOD_STATIC,      "Static"      }, { ENDLESS_MOD_RETALIATION,"Retaliation"},
		{ ENDLESS_MOD_AEGIS,       "AegisGate"   }, { ENDLESS_MOD_FLAKSCREEN, "FlakScreen" },
		{ ENDLESS_MOD_AUXREACTOR,  "AuxReactor"  }, { ENDLESS_MOD_LOWPROFILE, "LowProfile" },
		{ ENDLESS_MOD_GIANTKILLER, "GiantKiller" }, { ENDLESS_MOD_SHOCKWAVE,  "Shockwave"  },
		{ ENDLESS_MOD_STARCHARTS,  "StarCharts"  }, { ENDLESS_MOD_BREAKTHROUGH,"Breakthrough"},
		{ ENDLESS_MOD_SOFTLANDING, "SoftLanding" }, { ENDLESS_MOD_CLEANSIGNALS,"CleanSignals"},
		{ ENDLESS_MOD_TWINSEEK,    "TwinSeek"    }, { ENDLESS_MOD_HUNTER,     "Hunter"     },
		{ ENDLESS_MOD_TRUEAIM,     "TrueAim"     }, { ENDLESS_MOD_KILLSHOT,   "KillShot"   },
	};

	fprintf(f, "  Active mods:  0x%016llX", (unsigned long long)mods);
	if (mods == 0)
	{
		fprintf(f, "  (none)\n");
		return;
	}

	Uint64 known = 0;
	int n = 0;
	fprintf(f, "\n    ");
	for (size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); ++i)
	{
		known |= tbl[i].bit;
		if (mods & tbl[i].bit)
			fprintf(f, "%s%s", n++ ? ", " : "", tbl[i].name);
	}
	if (mods & ~known)  // bits with no table entry (a mod added since this table was written)
		fprintf(f, "%s(unknown 0x%016llX)", n++ ? ", " : "", (unsigned long long)(mods & ~known));
	fprintf(f, "\n");
}

void crashlog_write_game_state(FILE *f)
{
	if (f == NULL)
		return;

	fprintf(f, "\n================================================================\n");
	fprintf(f, "Game state\n");
	fprintf(f, "================================================================\n");

	// Mode and difficulty.
	fprintf(f, "Mode:          %s%s   (loaded=%s)\n",
	        mode_label(), extraGame ? " +extra" : "", gameLoaded ? "yes" : "no");
	fprintf(f, "Mode flags:    2P=%d linked=%d 1PAction=%d galaga=%d timedBattle=%d superTyrian=%d endless=%d superArcade=%d\n",
	        twoPlayerMode, twoPlayerLinked, onePlayerAction, galagaMode,
	        timedBattleMode, superTyrian, endlessMode, (int)superArcadeMode);
	fprintf(f, "Difficulty:    %d (initial %d)%s\n",
	        (int)difficultyLevel, (int)initialDifficulty, expertMode ? "   EXPERT MODE" : "");
	fprintf(f, "Episode:       %d (initial %d)\n", (int)episodeNum, (int)initial_episode_num);

	// Level.
	fprintf(f, "\nLevel:\n");
	fprintf(f, "  Name:         '%.10s'\n", trim_name(levelName));
	fprintf(f, "  mainLevel=%d nextLevel=%d saveLevel=%d  lvlFileNum=%d  song=%d\n",
	        (int)mainLevel, (int)nextLevel, (int)saveLevel, (int)lvlFileNum, (int)levelSong);
	fprintf(f, "  curLoc=%u  totalEnemy=%u killed=%u onScreen=%u  stillExploding=%d\n",
	        (unsigned)curLoc, (unsigned)totalEnemy, (unsigned)enemyKilled,
	        (unsigned)enemyOnScreen, enemyStillExploding);
	fprintf(f, "  scroll: stopBackgrounds=%d(num=%d) forceEvents=%d  parkedAbove=%u stallTicks=%u\n",
	        stopBackgrounds, (int)stopBackgroundNum, forceEvents,
	        (unsigned)enemyParkedAbove, (unsigned)mapStopStallTicks);
	// Report the contact-damage clamp because zero disables enemy ramming damage.
	fprintf(f, "  damageRate=%u\n", (unsigned)damageRate);
	if (levelTimer)
		fprintf(f, "  levelTimer countdown=%u\n", (unsigned)levelTimerCountdown);

	// On-screen object pools.
	PoolCensus c;
	take_pool_census(&c);
	fprintf(f, "\nOn-screen pools:\n");
	fprintf(f, "  Enemies:      %d active (%d shootable) / 100\n", c.enemiesActive, c.enemiesShootable);
	fprintf(f, "  Player shots: %d / %d\n", c.playerShots, MAX_PWEAPON);
	fprintf(f, "  Enemy shots:  %d / %d\n", c.enemyShots, ENEMY_SHOT_MAX);
	fprintf(f, "  Explosions:   %d / %d   (repeating %d / %d)\n",
	        c.explosions, MAX_EXPLOSIONS, c.repExplosions, MAX_REPEATING_EXPLOSIONS);
	fprintf(f, "  Sparks:       %d live (cap %d)\n", c.sparks, MAX_SUPERPIXELS);

	// Read live enemies from static arrays; flag stuck and orphaned map-stop blockers.
	{
		int shown = 0;
		fprintf(f, "\nLive enemies (idx: ex,ey exc,eyc excc,eycc link armor type edmg):\n");
		for (int i = 0; i < 100 && shown < 24; ++i)
		{
			if (enemyAvail[i] == 1)
				continue;
			++shown;
			bool stuck = enemy[i].ey <= -58 && enemy[i].eyc <= 0 &&
			             enemy[i].eycc <= 0 && enemy[i].fixedmovey <= 0;
			bool rescued = false;
			if (stuck && enemy[i].linknum != 0)
			{
				for (int e = 0; e < 100 && !rescued; ++e)
					rescued = enemyAvail[e] != 1 && enemy[e].linknum == enemy[i].linknum &&
					          !(enemy[e].ey <= -58 && enemy[e].eyc <= 0 &&
					            enemy[e].eycc <= 0 && enemy[e].fixedmovey <= 0);
			}
			fprintf(f, "  %2d: %4d,%-4d  %3d,%-3d  %3d,%-3d  L%-3d a%-3d t%-4d e%d%s\n",
			        i, (int)enemy[i].ex, (int)enemy[i].ey, (int)enemy[i].exc, (int)enemy[i].eyc,
			        (int)enemy[i].excc, (int)enemy[i].eycc, (int)enemy[i].linknum,
			        (int)enemy[i].armorleft, (int)enemy[i].enemytype, (int)enemy[i].edamaged,
			        !stuck ? "" : rescued ? "  <-- stuck-above (group reachable)"
			                              : "  <-- stuck-above (orphaned)");
		}
		if (shown == 0)
			fprintf(f, "  (none)\n");
	}

	// Players and loadout.
	for (int p = 0; p < (twoPlayerMode ? 2 : 1); ++p)
		write_player(f, p);

	// Custom weapons.
	fprintf(f, "\nCustom weapons: %s\n", customWeaponEnabled ? "ENABLED" : "disabled");
	if (customWeaponEnabled)
	{
		fprintf(f, "  Name:         '%.30s'\n", customWeaponName);
		fprintf(f, "  Port=%d sidekickSlot=%d equip=%d cost=%d\n",
		        customWeaponPort, customSidekickSlot, customWeaponEquipSlot, customWeaponCost);
		if (coop_mode_active())
		{
			// Both players' reserved slots: a mismatch here is a Campaign desync waiting to happen.
			fprintf(f, "  Owner slots:  P1 port=%d sk=%d, P2 port=%d sk=%d\n",
			        customWeaponOwnerPort[0], customSidekickOwnerSlot[0],
			        customWeaponOwnerPort[1], customSidekickOwnerSlot[1]);
		}
		fprintf(f, "  Library:      %d weapon(s), editing slot %d\n",
		        customWeaponLibCount, customWeaponCurrentSlot);
	}

	// Endless run.
	if (endlessMode)
	{
		fprintf(f, "\nEndless run:\n");
		fprintf(f, "  Depth:        %d\n", endlessRunDepth);
		fprintf(f, "  Mode:         %s%s\n", endlessRunModeName(endlessRunMode),
		        endlessLockedSortie ? "  (outpost LOCKED)" : "");
		fprintf(f, "  Kills:        %d  (bosses %d)\n", endlessRunKills, endlessRunBossKills);
		// Read the ledger without auditing from the crash path. Print sinks as negative values.
		fprintf(f, "  Cash earned:  %llu  (spent %llu)\n",
		        (unsigned long long)endlessRunCashEarned, (unsigned long long)endlessRunCashSpent);
		for (int i = 0; i < ENDLESS_CASH_SOURCES; ++i)
			if (endlessCashBySource[i] != 0)
				fprintf(f, "    %-15s %llu\n", endlessCashSourceName((EndlessCashSource)i),
				        (unsigned long long)endlessCashBySource[i]);
		for (int i = 0; i < ENDLESS_CASH_SINKS; ++i)
			if (endlessCashBySink[i] != 0)
				fprintf(f, "    %-15s -%llu\n", endlessCashSinkName((EndlessCashSink)i),
				        (unsigned long long)endlessCashBySink[i]);
		write_endless_mods(f, endlessActiveMods);
		fprintf(f, "  Armor bonus:  %d / %d\n", endlessArmorBonus[0], endlessArmorBonus[1]);
		const char *seed = endlessSeedString();
		if (seed != NULL)
			fprintf(f, "  Seed:         '%.*s'\n", ENDLESS_SEED_MAXLEN, seed);
		fprintf(f, "  Base level:   ep %d, level %d  '%.10s'\n",
		        endlessBaseLevelEpisode(), endlessBaseLevelSection(), trim_name(endlessBaseLevelName()));
		if (endlessPrevLevelName()[0] != '\0')
			fprintf(f, "  Prev zone:    ep %d, level %d  '%.10s'\n",
			        endlessPrevLevelEpisode(), endlessPrevLevelSection(), trim_name(endlessPrevLevelName()));
		const int recentN = endlessRecentLevelCount();
		if (recentN > 0)
		{
			fprintf(f, "  Recent zones: ");  // newest first; the anti-repeat window the next pick avoids
			for (int i = 0; i < recentN; ++i)
				fprintf(f, "%s%d:%d", i ? ", " : "",
				        endlessRecentLevelEpisode(i), endlessRecentLevelSection(i));
			fprintf(f, "\n");
		}
	}

	// Include campaign debug modifiers, perks, and pinned settings in the report.
	if (!endlessMode && endlessCampaignMods)
	{
		fprintf(f, "\nEndless effects (debug, in a normal game):\n");
		fprintf(f, "  Virtual zone: %d\n", endlessRunDepth + 1);
		write_endless_mods(f, endlessActiveMods);
		int perks = 0;
		for (int p = 0; p < endlessPerkCount(); ++p)
		{
			const int n = endlessPerkGetOwned(p);
			if (n > 0)
			{
				fprintf(f, "%s%s x%d", perks++ ? ", " : "  Perks:        ", endlessPerkName(p), n);
			}
		}
		fprintf(f, "%s", perks ? "\n" : "  Perks:        none\n");
	}

	// Pinned scaling levers force a value past the ramp, so a "this difficulty is impossible"
	// report is worth reading differently. Listed whenever any are set, endless or not.
	if (endlessScalingOverrideCount() > 0)
	{
		fprintf(f, "\nScaling overrides:\n");
		for (int i = 0; i < ESO_COUNT; ++i)
			if (endlessScalingOverride[i].active)
				fprintf(f, "  %-16s %d  (stock %d)\n", endlessScalingOverrideName(i),
				        endlessScalingOverride[i].value, endlessScalingOverrideStock(i));
	}

	// Cheats and debug toggles.
	fprintf(f, "\nCheats / debug:\n");
	fprintf(f, "  debug=%d youAreCheating=%d noclip=%d hitboxOverlay=%d perfOverlay=%d\n",
	        debug, youAreCheating, (int)noclipMode, debugHitboxOverlay, debugPerfOverlay);
	fprintf(f, "  infShot=%d infArmor=%d infShield=%d infGen=%d noEnemyFire=%d instCharge=%d infSideAmmo=%d\n",
	        infiniteShot, cheatInfiniteArmor, cheatInfiniteShields, cheatInfiniteGenerator,
	        cheatNoEnemyFire, cheatInstantCharge, cheatInfiniteSidekickAmmo);

	// Netcode: connection health, sync counters, desync/rollback state.
	if (isNetworkGame)
		network_write_diagnostics(f);

	// Video and rendering.
	fprintf(f, "\nVideo / render:\n");
	fprintf(f, "  Logical size: %dx%d   %s   vsync=%d\n", vga_width, vga_height,
	        fullscreen_display < 0 ? "windowed" : "fullscreen", output_vsync);
	fprintf(f, "  Scaling:      %s   scaler=%u %s\n",
	        ((unsigned)scaling_mode < (unsigned)ScalingMode_MAX) ? scaling_mode_names[scaling_mode] : "?",
	        scaler, (scaler < scalers_count) ? scalers[scaler].name : "?");
	fprintf(f, "  Smooth motion=%s  supersample=%d  render_list: rec=%d count=%zu  alpha=%.3f\n",
	        smoothMotion ? "on" : "off", render_supersample, render_list_recording, rl_count(),
	        (double)debug_interp_alpha);
	fprintf(f, "  FPS:          %d (cap %d, show=%d)\n", current_fps, fps_cap, show_fps);

	// Audio.
	fprintf(f, "\nAudio:\n");
	fprintf(f, "  Song:         %u %s\n", song_playing,
	        (song_playing < MUSIC_NUM) ? musicTitle[song_playing] : "?");
	fprintf(f, "  Volume:       music=%u fx=%u   disabled: audio=%d music=%d samples=%d\n",
	        (unsigned)tyrMusicVolume, (unsigned)fxVolume, audio_disabled, music_disabled, samples_disabled);
	fprintf(f, "  Music device: %s\n",
	        ((unsigned)music_device < MUSIC_DEVICE_MAX) ? music_device_names[music_device] : "?");
	if (music_device == FLUIDSYNTH)  // fixed 4096-byte global; safe to scan
		fprintf(f, "  SoundFont:    %s%s\n",
		        soundfont[0] != '\0' ? soundfont : "(none)",
		        midi_soundfont_loaded ? " [loaded]" : " [NOT loaded]");
	else if (music_device == NATIVE_MIDI && soundfont[0] != '\0')
		fprintf(f, "  SoundFont:    %s (unused; native synth)\n", soundfont);
	fprintf(f, "  Sound queue: ");
	for (int i = 0; i < 8; ++i)
		fprintf(f, " %d", soundQueue[i]);
	fprintf(f, "\n");

	// Loaded enemy sprite banks.
	fprintf(f, "\nEnemy sprite banks:\n");
	for (int i = 0; i < 4; ++i)
		fprintf(f, "  bank %d: id=%-3d %s\n", i, (int)enemySpriteSheetIds[i],
		        enemySpriteSheets[i].data != NULL ? "loaded" : "EMPTY");

	// Miscellaneous engine state.
	fprintf(f, "\nMisc:\n");
	fprintf(f, "  Generator power: %u (last %u, add %u)\n", power, lastPower, powerAdd);
	fprintf(f, "  gameSpeed=%d fps_cap=%d netGame=%d\n", (int)gameSpeed, fps_cap, isNetworkGame);
	fprintf(f, "  demo: play=%d record=%d\n", play_demo, record_demo);
}
