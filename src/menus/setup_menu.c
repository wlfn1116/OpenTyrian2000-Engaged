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
#include "setup_menu.h"

#include "audio.h"
#include "config.h"
#include "console_platform.h"
#include "crashlog.h"
#include "custom_episode.h"
#include "custom_weapon.h"
#include "debug_flags.h"
#include "destruct.h"
#include "draw.h"
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "game_state.h"
#include "help.h"
#include "helptext.h"
#include "joystick.h"
#include "jukebox.h"
#include "keyboard.h"
#include "mouse.h"
#include "net_savexfer.h"
#include "opentyr.h"
#include "palette.h"
#include "params.h"
#include "picload.h"
#include "save_menu.h"
#include "sfx.h"
#include "ship_editor.h"
#include "sndmast.h"
#include "sprite.h"
#include "timing.h"
#include "title.h"
#include "touch_ui.h"
#include "video.h"
#include "video_scale.h"
#include "weapon_editor.h"
#include "xmas.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PLATFORM_HANDHELD
// Console and mobile video drivers own one always-fullscreen display.
static size_t getDisplayPickerItemsCount(void)
{
	return 1 + (size_t)SDL_GetNumVideoDisplays();
}

static const char *getDisplayPickerItem(size_t i, char *buffer, size_t bufferSize)
{
	if (i == 0)
		return "Window";

	snprintf(buffer, bufferSize, "Display %d", (int)i);
	return buffer;
}
#endif

static size_t getScalerPickerItemsCount(void)
{
	return (size_t)scalers_count;
}

static const char *getScalerPickerItem(size_t i, char *buffer, size_t bufferSize)
{
	(void)buffer, (void)bufferSize;

	return scalers[i].name;
}

static size_t getScalingModePickerItemsCount(void)
{
	return (size_t)ScalingMode_MAX;
}

static const char* getScalingModePickerItem(size_t i, char* buffer, size_t bufferSize)
{
	(void)buffer, (void)bufferSize;

	return scaling_mode_names[i];
}

/* Graphics: sub-pixel supersampling picker. */

// Indices map directly to Auto, Off, fixed 2x..5x, and Native supersampling.
static const char *const supersampleNames[] = { "Auto", "Off", "2x", "3x", "4x", "5x", "Native" };

static size_t getSupersamplePickerItemsCount(void) { return COUNTOF(supersampleNames); }
static const char* getSupersamplePickerItem(size_t i, char* buffer, size_t bufferSize)
{
	(void)buffer; (void)bufferSize;
	return supersampleNames[i];
}

enum { RENDER_THREAD_CHOICES_MAX = 8 };

static size_t renderThreadChoices(int choices[RENDER_THREAD_CHOICES_MAX])
{
	const int max = video_rows_max_threads();
	size_t n = 0;
	choices[n++] = 0;
	for (int t = 1; t <= max; t *= 2)
		choices[n++] = t;
	if (choices[n - 1] != max)
		choices[n++] = max;
	return n;
}

static size_t renderThreadChoiceIndex(void)
{
	int choices[RENDER_THREAD_CHOICES_MAX];
	const size_t n = renderThreadChoices(choices);
	for (size_t i = 0; i < n; ++i)
		if (choices[i] == render_threads)
			return i;
	return 0;
}

static size_t getRenderThreadsPickerItemsCount(void)
{
	int choices[RENDER_THREAD_CHOICES_MAX];
	return renderThreadChoices(choices);
}

static const char* getRenderThreadsPickerItem(size_t i, char* buffer, size_t bufferSize)
{
	int choices[RENDER_THREAD_CHOICES_MAX];
	renderThreadChoices(choices);
	if (choices[i] == 0)
		return "Auto";
	snprintf(buffer, bufferSize, "%d", choices[i]);
	return buffer;
}

/* Enhancement labels are indexed by their stored enum values. Keep each list in
 * enum order. */

#define NAME_PICKER(name, array)                                                     \
	static size_t name##Count(void) { return COUNTOF(array); }                       \
	static const char *name##Item(size_t i, char *buffer, size_t bufferSize)         \
	{                                                                                \
		(void)buffer; (void)bufferSize;                                              \
		return array[i];                                                             \
	}

static const char *const bossBarStyleNames[]  = { "Classic", "Enhanced" };
static const char *const bossBarLayoutNames[] = { "Top", "Bottom", "Left", "Right" };
static const char *const bossBarTwoNames[]    = { "Together", "Split", "Stacked" };
// Must match VulnerableCueMode.
static const char *const vulnCueNames[]       = { "Off", "Bosses", "All" };

static const char *const enemyBarLayoutNames[]   = { "Horizontal", "Vertical" };
static const char *const enemyBarPositionNames[] = { "Bottom", "Top", "Left", "Right", "Center" };

// Indexed by GaugeGradientDir (config.h). "Up" is the classic vertical gauge look; Left/Right
// run the gradient across the bar's 9-pixel width instead. The label names the bar's bright end.
static const char *const gaugeGradNames[] = { "Up", "Down", "Left", "Right" };

// Shared episode-data labels; the small font lacks a plus glyph, so use "Ep 4-5".
static const char *const episodeNames[] = { "Auto", "Ep 1-3", "Ep 4-5" };

// Indexed by the SUPER_SPARKS_* enum (config.h).
static const char *const sparkModeNames[] = { "Auto", "On", "Off" };

// Indexed by the CUSTOM_ENDLESS_* enum (custom_episode.h).
static const char *const customEndlessNames[] = { "Off", "Mixed", "Custom Only" };

// Indexed by SfxFilter (audio.h).
static const char *const sfxFilterNames[] = { "Off", "Light", "Strong" };
// Index 0 is no limit, then 1..SFX_COPY_LIMIT_MAX copies.
static const char *const sfxCopyLimitNames[] = { "Off", "1", "2", "3" };
// Indexed by RumbleStrength (joystick.h).
static const char *const rumbleNames[] = { "Off", "Light", "Strong" };
// Indexed by CrtFilter (video.h).
static const char *const crtFilterNames[] = { "Off", "Scanlines", "Full" };
static const char *const crtStrengthNames[] = { "Light", "Medium", "Strong" };
static const char *const crtCurvatureNames[] = { "Off", "Subtle", "Strong" };
static const char *const crtMaskNames[] = { "Off", "Grille", "Slot" };
static const char *const crtCornerNames[] = { "Square", "Rounded" };
static const char *const crtRollingBarNames[] = { "Off", "Subtle", "Strong" };

SDL_COMPILE_TIME_ASSERT(sfx_filter_names, COUNTOF(sfxFilterNames) == SFX_FILTER_COUNT);
SDL_COMPILE_TIME_ASSERT(sfx_copy_names, COUNTOF(sfxCopyLimitNames) == SFX_COPY_LIMIT_MAX + 1);
SDL_COMPILE_TIME_ASSERT(rumble_names, COUNTOF(rumbleNames) == RUMBLE_COUNT);
SDL_COMPILE_TIME_ASSERT(crt_filter_names, COUNTOF(crtFilterNames) == CRT_FILTER_COUNT);
SDL_COMPILE_TIME_ASSERT(crt_strength_names, COUNTOF(crtStrengthNames) == CRT_STRENGTH_COUNT);
SDL_COMPILE_TIME_ASSERT(crt_curvature_names, COUNTOF(crtCurvatureNames) == CRT_CURVATURE_COUNT);
SDL_COMPILE_TIME_ASSERT(crt_mask_names, COUNTOF(crtMaskNames) == CRT_MASK_COUNT);
SDL_COMPILE_TIME_ASSERT(crt_corner_names, COUNTOF(crtCornerNames) == CRT_CORNERS_COUNT);
SDL_COMPILE_TIME_ASSERT(crt_rolling_bar_names,
                        COUNTOF(crtRollingBarNames) == CRT_ROLLING_BAR_COUNT);

NAME_PICKER(bossBarStyle, bossBarStyleNames)
NAME_PICKER(bossBarLayout, bossBarLayoutNames)
NAME_PICKER(bossBarTwo, bossBarTwoNames)
NAME_PICKER(vulnCue, vulnCueNames)
NAME_PICKER(enemyBarLayout, enemyBarLayoutNames)
NAME_PICKER(enemyBarPosition, enemyBarPositionNames)
NAME_PICKER(gaugeGrad, gaugeGradNames)
NAME_PICKER(episode, episodeNames)
NAME_PICKER(sparkMode, sparkModeNames)
NAME_PICKER(customEndless, customEndlessNames)
NAME_PICKER(sfxFilter, sfxFilterNames)
NAME_PICKER(sfxCopyLimit, sfxCopyLimitNames)
NAME_PICKER(rumble, rumbleNames)
NAME_PICKER(crtFilter, crtFilterNames)
NAME_PICKER(crtStrength, crtStrengthNames)
NAME_PICKER(crtCurvature, crtCurvatureNames)
NAME_PICKER(crtMask, crtMaskNames)
NAME_PICKER(crtCorner, crtCornerNames)
NAME_PICKER(crtRollingBar, crtRollingBarNames)

/* The preset picker shows Custom but does not let the user apply it. */
static size_t enhPresetCount(void)
{
	return ENH_PRESET_COUNT;
}
static const char *enhPresetItem(size_t i, char *buffer, size_t bufferSize)
{
	(void)buffer; (void)bufferSize;
	return enhancementPresetName((EnhancementPreset)i);
}

/* Sound: music synthesizer picker. */

// FluidSynth and Native MIDI require WITH_MIDI; other builds force OPL.
static size_t getMusicDeviceItemsCount(void)
{
#ifdef WITH_MIDI
	return MUSIC_DEVICE_MAX;
#else
	return 1;  // only OPL3 works without a MIDI build (e.g. the Switch port)
#endif
}
static const char* getMusicDeviceItem(size_t i, char* buffer, size_t bufferSize)
{
	(void)buffer; (void)bufferSize;
	return music_device_names[i];
}

// Toggle the persisted Christmas assets and reload their shapes and voices.
// Enabling fails when either Christmas data file is missing.
static bool toggle_xmas_mode(void)
{
	if (!xmas_set(!xmas))
		return false;  // can't enable Christmas without its data files

	override_xmas = true;      // honour this explicit choice over date auto-detection
	xmasMode = xmas ? 1 : 0;   // persist the choice

	return true;
}

// Supersampling uses a plain same-size scaler so gameplay and menus match.
static void enforcePlainScalerForSupersample(void)
{
	if (render_supersample != 1 && !scaler_is_plain(scaler))
	{
		const uint plain = scaler_plain_equivalent(scaler);
		if (!init_scaler(plain))
			init_scaler(scaler);  // shouldn't happen; keep a working scaler
	}
}

// Cycle the three player-facing Sidekick Autofire modes, skipping debug-only
// Fast. Both menus edit the same setting.
static void cycleSidekickAutofire(int dir)
{
	int v = chargeSidekickAutofire;
	do
		v = (v + CHARGE_AUTOFIRE_NUM + dir) % CHARGE_AUTOFIRE_NUM;
	while (v == CHARGE_AUTOFIRE_FAST);
	chargeSidekickAutofire = (JE_byte)v;
}

/* Option-menu rows. Submenus name their target in `submenu`; repeated item rows
 * derive their array slot from id minus the run's base. */
typedef enum
{
	MENU_ITEM_NONE = 0,
	MENU_ITEM_DONE,
	MENU_ITEM_SUBMENU,              // opens this row's `submenu`; carries no value of its own

	/* Graphics. */
	MENU_ITEM_DISPLAY,
	MENU_ITEM_SCALER,
	MENU_ITEM_SCALING_MODE,
	MENU_ITEM_SMOOTH_MOTION,
	MENU_ITEM_SUPERSAMPLE,
	MENU_ITEM_SUBPIXEL_FX,
	MENU_ITEM_RENDER_THREADS,
	MENU_ITEM_VSYNC,
	MENU_ITEM_FPS,
	MENU_ITEM_SHOW_FPS,

	/* Sound, and the Setup sensitivity slider. */
	MENU_ITEM_MUSIC_VOLUME,
	MENU_ITEM_SOUND_VOLUME,
	MENU_ITEM_MUSIC_DEVICE,         // music synthesizer: OPL3 / FluidSynth / Native MIDI
	MENU_ITEM_ARMOR_ALARM,          // low-armor WARNING siren on/off
	MENU_ITEM_LINK_SOUNDS,          // 2P fuse/unfuse clink+spring on/off
	MENU_ITEM_SFX_FILTER,           // low-pass on the mixed sound effects
	MENU_ITEM_SFX_COPY_LIMIT,       // copies of one sound effect allowed at once
	MENU_ITEM_SHIP_SENS,            // "Sensitivity" slider: touch on consoles, mouse on desktop
	MENU_ITEM_RUMBLE,               // controller vibration strength
	MENU_ITEM_TOUCH_OPACITY,        // on-screen button opacity, touch ports only

	/* Enhancements. */
	MENU_ITEM_ENH_PRESET,           // writes the whole enhancement set at once (config.c)

	/* Enhancements -> Visuals. */
	MENU_ITEM_EXTRA_PARALLAX,
	MENU_ITEM_MIRRORED_LAYERS,      // over-panned layer edges mirror (works in both parallax modes)
	MENU_ITEM_EXTRA_SPARKS,
	MENU_ITEM_SPECIAL_TINT,         // flare specials grade the whole screen their colour
	MENU_ITEM_UNUSED_SPRITES,       // spend the shop sheet's unreferenced icons (episodes.c)
	MENU_ITEM_CRT_FILTER,           // scanline / full CRT layers over the presented frame
	MENU_ITEM_CRT_STRENGTH,
	MENU_ITEM_CRT_CURVATURE,
	MENU_ITEM_CRT_MASK,
	MENU_ITEM_CRT_CORNERS,
	MENU_ITEM_CRT_ROLLING_BAR,

	/* Enhancements -> Heads-Up Display. */
	MENU_ITEM_ENEMY_BARS,
	MENU_ITEM_ENEMY_BAR_LAYOUT,
	MENU_ITEM_ENEMY_BAR_POS,
	MENU_ITEM_ENEMY_BAR_OPACITY,
	MENU_ITEM_BOSS_BAR_STYLE,
	MENU_ITEM_BOSS_BAR_LAYOUT,
	MENU_ITEM_BOSS_BAR_TWO,
	MENU_ITEM_BOSS_VULN_CUE,
	MENU_ITEM_GAUGE_GRAD_GEN,
	MENU_ITEM_GAUGE_GRAD_SHIELD,
	MENU_ITEM_GAUGE_GRAD_ARMOR,
	MENU_ITEM_GAUGE_FLASH_SHIELD,
	MENU_ITEM_GAUGE_FLASH_ARMOR,
	MENU_ITEM_TOUCH_SIDEKICKS,      // touch ports only: draw the sidekick fire buttons
	MENU_ITEM_TOUCH_NAV,

	/* Enhancements -> Weapons. */
	MENU_ITEM_CHARGE_LASER,
	MENU_ITEM_SIDEKICK_AUTOFIRE,    // charge-sidekick autofire (shares chargeSidekickAutofire with the debug menu)
	MENU_ITEM_WALLOP_BOLT,          // Wallop Beam only: the ep4/5 second bolt per volley

	/* Enhancements -> Episode Versions. */
	MENU_ITEM_ZICA_BASE,

	/* Enhancements -> Gameplay. */
	MENU_ITEM_SHOT_HITBOXES,        // collide projectiles from the middle of their sprites (game_loop.c)
	MENU_ITEM_GUIDED_AIM,           // weapon-table homing steers toward the enemy's screen x (shots.c)
	MENU_ITEM_BASE_DISPENSERS,      // wake the dormant dispenser bases (enemy 80-83)
	MENU_ITEM_CUSTOM_ENDLESS,       // custom episodes in the Endless pool; hidden unless some exist
	MENU_ITEM_CLEAR_CLV,            // delete every custom episode; hidden unless some exist
	MENU_ITEM_ARCADE_LIFE_BOOST,    // arcade lives scale the shield/armour ceilings
	MENU_ITEM_ARCADE_RANDOM_BALLS,  // arcade weapon balls re-rolled within their class
	MENU_ITEM_ARCADE_REAR_SCALE,    // arcade rear gun fires at the life count too

	/* Setup -> Diagnostics. */
	MENU_ITEM_DEBUG_MODE,
	MENU_ITEM_NET_LOG,              // write this session's log/opentyrian_net_<time>.log during online play
	MENU_ITEM_CLEAR_LOGS,           // delete every stored log, crash and net alike (console-only row)

	/* Title-screen Extra menu. */
	MENU_ITEM_INSTRUCTIONS,
	MENU_ITEM_JUKEBOX,
	MENU_ITEM_DESTRUCT,
	MENU_ITEM_SUPERTYRIAN,
	MENU_ITEM_SHIP_EDITOR,
	MENU_ITEM_WEAPON_EDITOR,
	MENU_ITEM_TRANSFER_SAVE_UPLOAD,
	MENU_ITEM_TRANSFER_SAVE_DOWNLOAD,
	MENU_ITEM_TRANSFER_SAVES_UPLOAD,
	MENU_ITEM_TRANSFER_SAVES_DOWNLOAD,
	MENU_ITEM_TRANSFER_SHIPS_UPLOAD,
	MENU_ITEM_TRANSFER_SHIPS_DOWNLOAD,
	MENU_ITEM_TRANSFER_WEAPONS_UPLOAD,
	MENU_ITEM_TRANSFER_WEAPONS_DOWNLOAD,
	MENU_ITEM_TRANSFER_LEVELS_UPLOAD,
	MENU_ITEM_TRANSFER_LEVELS_DOWNLOAD,
	MENU_ITEM_TRANSFER_CUSTOM_UPLOAD,
	MENU_ITEM_TRANSFER_CUSTOM_DOWNLOAD,
	MENU_ITEM_TRANSFER_ALL_UPLOAD,
	MENU_ITEM_TRANSFER_ALL_DOWNLOAD,
	MENU_ITEM_XMAS,
	MENU_ITEM_RICH_MODE,
	MENU_ITEM_CONSTANT_PLAY,
	MENU_ITEM_CONSTANT_DIE,

	/* Id runs: one row per array slot, the offset from the base picking the slot. */
	MENU_ITEM_SPARKS_MODE_BASE,                                              // +0..SSW_COUNT-1
	MENU_ITEM_SPARKS_CAP_BASE = MENU_ITEM_SPARKS_MODE_BASE + SSW_COUNT,      // +0..SSW_COUNT-1
	MENU_ITEM_EPDIFF_BASE     = MENU_ITEM_SPARKS_CAP_BASE + SSW_COUNT,       // +0..EDW_COUNT-1
	MENU_ITEM_REWORK_BASE     = MENU_ITEM_EPDIFF_BASE + EDW_COUNT,
	// keep LAST: +0..+8 are the 9 arcade ships
	MENU_ITEM_ARCADE_SHIP_BASE = MENU_ITEM_REWORK_BASE + REWORK_COUNT,
} MenuItemId;

typedef enum
{
	MENU_NONE = 0,
	MENU_SETUP,
	MENU_GRAPHICS,
	MENU_SOUND,
	MENU_ENHANCEMENTS,
	MENU_VISUALS,
	MENU_CRT,
	MENU_HUD,
	MENU_ENEMY_BARS,
	MENU_BOSS_BARS,
	MENU_GAUGES,
	MENU_WEAPONS,
	MENU_SPARK_TRAILS,
	MENU_SPARK_CAPS,
	MENU_GAMEPLAY,
	MENU_ARCADE_MODES,  // Gameplay -> Arcade Modes (settings); MENU_ARCADE below is the ship picker
	MENU_EPISODE_VERSIONS,
	MENU_FIRING_SOUNDS,
	MENU_SHOP_PICTURES,
	MENU_REWORKS,
	MENU_DIAGNOSTICS,
	MENU_EXTRA,
	MENU_SECRET_MODES,
	MENU_TRANSFER,
	MENU_TRANSFER_SAVE,
	MENU_TRANSFER_SAVES,
	MENU_TRANSFER_SHIPS,
	MENU_TRANSFER_WEAPONS,
	MENU_TRANSFER_LEVELS,
	MENU_TRANSFER_CUSTOM,
	MENU_TRANSFER_ALL,
	MENU_ARCADE,
	MENU_CMDLINE,
} MenuId;

typedef struct
{
	MenuItemId id;
	const char *name;
	const char *description;
	MenuId submenu;  // MENU_ITEM_SUBMENU rows: the menu this row opens
	size_t (*getPickerItemsCount)(void);
	const char *(*getPickerItem)(size_t i, char *buffer, size_t bufferSize);
} MenuItem;

static bool isMenuItemVisible(const MenuItem *item)
{
	// Invalid containers still keep Clear visible.
	if (item->id == MENU_ITEM_SUBMENU && item->submenu == MENU_TRANSFER_LEVELS)
		return customEpisodeCount() > 0;
	if (item->id == MENU_ITEM_CUSTOM_ENDLESS)
		return customEpisodeCount() > 0;
	if (item->id == MENU_ITEM_CLEAR_CLV)
		return customEpisodeAnyPresent();
	if (item->id == MENU_ITEM_RUMBLE)
		return joystick_can_rumble();
	if ((item->id == MENU_ITEM_SUBMENU && item->submenu == MENU_CRT) ||
	    item->id == MENU_ITEM_CRT_FILTER || item->id == MENU_ITEM_CRT_STRENGTH ||
	    item->id == MENU_ITEM_CRT_MASK || item->id == MENU_ITEM_CRT_CORNERS ||
	    item->id == MENU_ITEM_CRT_ROLLING_BAR)
		return crt_filter_available();
	if (item->id == MENU_ITEM_CRT_CURVATURE)
		return crt_curvature_available();
	return true;
}

// Slot an id-run row edits, or -1 if the id is not in that run.
static int menuItemRunSlot(MenuItemId id, MenuItemId base, int count)
{
	return (id >= base && id < (MenuItemId)(base + count)) ? (int)(id - base) : -1;
}

// A row backed by an int setting reads and writes it through its own picker list, so
// the two have to come as a pair; a table row missing one is a bug, not a mode.
static bool menuItemHasPicker(const MenuItem *item)
{
	return item->getPickerItemsCount != NULL && item->getPickerItem != NULL;
}

/* Integer settings that index their row's picker list. One map serves drawing, arrows, and the
 * picker; settings with side effects keep explicit cases where they are drawn. */
static int *menuItemIntSetting(MenuItemId id)
{
	int slot;

	if ((slot = menuItemRunSlot(id, MENU_ITEM_SPARKS_MODE_BASE, SSW_COUNT)) >= 0)
		return &superSparkMode[slot];
	if ((slot = menuItemRunSlot(id, MENU_ITEM_EPDIFF_BASE, EDW_COUNT)) >= 0)
		return &epDiffMode[slot];

	switch (id)
	{
	case MENU_ITEM_BOSS_BAR_STYLE:   return &bossBarStyle;
	case MENU_ITEM_BOSS_BAR_LAYOUT:  return &bossBarLayout;
	case MENU_ITEM_BOSS_BAR_TWO:     return &bossBarTwoMode;
	case MENU_ITEM_BOSS_VULN_CUE:    return &vulnerableCue;
	case MENU_ITEM_ENEMY_BAR_LAYOUT: return &enemyBarLayout;
	case MENU_ITEM_ENEMY_BAR_POS:    return &enemyBarPosition;
	case MENU_ITEM_GAUGE_GRAD_GEN:   return &gaugeGradGenerator;
	case MENU_ITEM_GAUGE_GRAD_SHIELD: return &gaugeGradShield;
	case MENU_ITEM_GAUGE_GRAD_ARMOR: return &gaugeGradArmor;
	case MENU_ITEM_ZICA_BASE:        return &zicaLaserBase;
	case MENU_ITEM_WALLOP_BOLT:      return &wallopSecondBolt;
	case MENU_ITEM_CUSTOM_ENDLESS:   return &customEndlessMode;
	case MENU_ITEM_SFX_FILTER:       return &sfx_filter;
	case MENU_ITEM_SFX_COPY_LIMIT:   return &sfx_copy_limit;
	case MENU_ITEM_RUMBLE:           return &rumble_strength;
	case MENU_ITEM_CRT_FILTER:       return &crt_filter;
	case MENU_ITEM_CRT_STRENGTH:     return &crt_strength;
	case MENU_ITEM_CRT_CURVATURE:    return &crt_curvature;
	case MENU_ITEM_CRT_MASK:         return &crt_mask;
	case MENU_ITEM_CRT_CORNERS:      return &crt_corners;
	case MENU_ITEM_CRT_ROLLING_BAR:  return &crt_rolling_bar;
	default:                         return NULL;
	}
}

static bool subpixelFxAvailable(void)
{
#ifdef __vita__
	return smoothMotion;
#else
	return smoothMotion && render_supersample != 1;
#endif
}

/* Plain toggles stop here; toggles with side effects keep dedicated cases below. */
static bool *menuItemBoolSetting(MenuItemId id)
{
	int slot;

	if ((slot = menuItemRunSlot(id, MENU_ITEM_SPARKS_CAP_BASE, SSW_COUNT)) >= 0)
		return &superSparkClassicCap[slot];
	if ((slot = menuItemRunSlot(id, MENU_ITEM_REWORK_BASE, REWORK_COUNT)) >= 0)
		return &reworkEnabled[slot];

	switch (id)
	{
	case MENU_ITEM_SHOW_FPS:            return &show_fps;
	case MENU_ITEM_SUBPIXEL_FX:         return &smoothie_full_res;
	case MENU_ITEM_ARMOR_ALARM:         return &armorAlarm;
	case MENU_ITEM_LINK_SOUNDS:         return &linkSounds;
	case MENU_ITEM_EXTRA_PARALLAX:      return &extraParallax;
	case MENU_ITEM_MIRRORED_LAYERS:     return &mirroredLayers;
	case MENU_ITEM_EXTRA_SPARKS:        return &extraSparks;
	case MENU_ITEM_SPECIAL_TINT:        return &specialScreenTint;
	case MENU_ITEM_ENEMY_BARS:          return &enemyBars;
	case MENU_ITEM_GAUGE_FLASH_SHIELD:  return &gaugeFlashShield;
	case MENU_ITEM_GAUGE_FLASH_ARMOR:   return &gaugeFlashArmor;
	case MENU_ITEM_TOUCH_SIDEKICKS:     return &touchSidekickButtons;
	case MENU_ITEM_TOUCH_NAV:           return &touchNavButtons;
	case MENU_ITEM_CHARGE_LASER:        return &chargeLaserCannon;
	case MENU_ITEM_GUIDED_AIM:          return &guidedShotScreenAim;
	case MENU_ITEM_BASE_DISPENSERS:     return &restoreBaseDispensers;
	case MENU_ITEM_ARCADE_LIFE_BOOST:   return &arcadeLifeBoost;
	case MENU_ITEM_ARCADE_RANDOM_BALLS: return &arcadeRandomBalls;
	case MENU_ITEM_ARCADE_REAR_SCALE:   return &arcadeRearGunScale;
	case MENU_ITEM_DEBUG_MODE:          return &debugMode;
	case MENU_ITEM_RICH_MODE:           return &richMode;
	case MENU_ITEM_CONSTANT_PLAY:       return &constantPlay;
	case MENU_ITEM_CONSTANT_DIE:        return &constantDie;
	default:                            return NULL;
	}
}

/* Firing Sounds previews the selected sound and Rumble a hull-hit pulse; other rows use the normal
 * menu sound. */
static void playMenuItemSample(MenuItemId id, JE_byte fallback)
{
	const int slot = menuItemRunSlot(id, MENU_ITEM_EPDIFF_BASE, EDW_COUNT);
	const JE_byte sound = slot >= 0 ? JE_epDiffFiringSound(slot, epDiffMode[slot]) : 0;

	if (id == MENU_ITEM_RUMBLE)
		joystick_rumble(0, 0.55f, 0.35f, 220);

	JE_playSampleNum(sound != 0 ? sound : fallback);
}

/* Adjust a setup-menu item's value in response to left/right input (dir is -1
 * or +1). Items without a cyclable value are ignored. */
static void adjustMenuItemValue(const MenuItem *item, int dir)
{
	const MenuItemId id = item->id;

	int *const intSetting = menuItemIntSetting(id);
	if (intSetting != NULL && menuItemHasPicker(item))
	{
		const int count = (int)item->getPickerItemsCount();
		*intSetting = (*intSetting + count + dir) % count;
		playMenuItemSample(id, S_CURSOR);
		return;
	}

	bool *const boolSetting = menuItemBoolSetting(id);
	if (boolSetting != NULL)
	{
		if (id == MENU_ITEM_SUBPIXEL_FX && !subpixelFxAvailable())
		{
			JE_playSampleNum(S_SPRING);
			return;
		}
		*boolSetting = !*boolSetting;
		JE_playSampleNum(S_CURSOR);
		return;
	}

	switch (id)
	{
	case MENU_ITEM_ENH_PRESET:
	{
		// Custom joins the cycle once the player has a set of their own to come back to.
		int next = (int)enhancementPresetState();
		for (int step = 0; step < ENH_PRESET_COUNT; ++step)
		{
			next = (next + ENH_PRESET_COUNT + dir) % ENH_PRESET_COUNT;
			if (next != ENH_PRESET_CUSTOM || enhancementCustomAvailable())
				break;
		}

		enhancementApplyPreset((EnhancementPreset)next);
		JE_playSampleNum(S_CURSOR);
		break;
	}
	case MENU_ITEM_MUSIC_VOLUME:
		JE_playSampleNum(S_CURSOR);
		JE_changeVolume(&tyrMusicVolume, dir * 8, &fxVolume, 0);
		break;
	case MENU_ITEM_SOUND_VOLUME:
		JE_changeVolume(&tyrMusicVolume, 0, &fxVolume, dir * 8);
		JE_playSampleNum(S_CURSOR);
		break;
	case MENU_ITEM_SHIP_SENS:
		ship_sensitivity = MIN(MAX(0, ship_sensitivity + dir * 8), SHIP_SENS_MAX);
		JE_playSampleNum(S_CURSOR);
		break;
	case MENU_ITEM_TOUCH_OPACITY:
		touchButtonOpacity = MIN(MAX(0, touchButtonOpacity + dir * 5), TOUCH_OPACITY_MAX);
		JE_playSampleNum(S_CURSOR);
		break;
	case MENU_ITEM_FPS:
		if (dir > 0)
			fps_cap += 5;
		else
			fps_cap = fps_cap > 5 ? fps_cap - 5 : 0;
		set_fps(fps_cap);
		JE_playSampleNum(S_CURSOR);
		break;
	case MENU_ITEM_SUPERSAMPLE:
		if (smoothMotion)
		{
			render_supersample = (render_supersample + (int)COUNTOF(supersampleNames) + dir) % (int)COUNTOF(supersampleNames);
			enforcePlainScalerForSupersample();
		}
		JE_playSampleNum(S_CURSOR);
		break;
	case MENU_ITEM_RENDER_THREADS:
	{
		int choices[RENDER_THREAD_CHOICES_MAX];
		const int count = (int)renderThreadChoices(choices);
		render_threads = choices[((int)renderThreadChoiceIndex() + count + dir) % count];
		JE_playSampleNum(S_CURSOR);
		break;
	}
	case MENU_ITEM_VSYNC:
		set_vsync(!output_vsync);
		JE_playSampleNum(S_CURSOR);
		break;
	case MENU_ITEM_MUSIC_DEVICE:
	{
		const int n = (int)getMusicDeviceItemsCount();
		// Step past FluidSynth when there is no SoundFont for it to load; the picker
		// grays it out for the same reason. At most n steps, so this always lands.
		int next = music_device;
		for (int i = 0; i < n; ++i)
		{
			next = (next + n + dir) % n;
			if (next != FLUIDSYNTH || soundfont_available())
				break;
		}
		music_device = (MusicDevice)next;
		restart_audio();  // apply live (tears down + re-inits the synth; see audio.c)
		JE_playSampleNum(S_CURSOR);
		break;
	}
	case MENU_ITEM_NET_LOG:
		crashlog_set_netlog_enabled(!crashlog_get_netlog_enabled());
		JE_playSampleNum(S_CURSOR);
		break;
	case MENU_ITEM_ENEMY_BAR_OPACITY:
		enemyBarOpacity = dir > 0 ? MIN(100, enemyBarOpacity + 5) : MAX(0, enemyBarOpacity - 5);
		JE_playSampleNum(S_CURSOR);
		break;
	case MENU_ITEM_SMOOTH_MOTION:
		set_smooth_motion(!smoothMotion);
		enforcePlainScalerForSupersample();  // turning on re-arms Auto; scaler rule applies
		JE_playSampleNum(S_CURSOR);
		break;
	case MENU_ITEM_XMAS:
		JE_playSampleNum(toggle_xmas_mode() ? S_CURSOR : S_SPRING);
		break;
	case MENU_ITEM_UNUSED_SPRITES:
		unusedShopSprites = !unusedShopSprites;
		JE_applyUnusedShopSprites();  // repaint the item table now, not at the next episode load
		JE_playSampleNum(S_CURSOR);
		break;
	case MENU_ITEM_SHOT_HITBOXES:
		centeredShotHitboxes = !centeredShotHitboxes;
		JE_playSampleNum(S_CURSOR);
		break;
	case MENU_ITEM_SIDEKICK_AUTOFIRE:
		cycleSidekickAutofire(dir);
		JE_playSampleNum(S_CURSOR);
		break;
	default:
		break;
	}
}

static const Uint8 menuUnusedBanks[] = { 1, 5, 9, 11, 12, 13 };

static void drawArcadeShip(unsigned int i, int xCenter, int yTop, Palette shown)
{
	enum { SCALE = 2, HULL_W = 24, HULL_H = 28 };

	const JE_word gr = ships[SAShip[i]].shipgraphic;
	Sprite2_array *sheet = &spriteSheet9;
	unsigned int hull[2] = { gr, 0 };
	unsigned int hulls = 1;
	if (gr == 0 || gr == 1)
	{
		hull[0] = (gr == 0) ? 13 : 220;
		hull[1] = (gr == 0) ? 51 : 222;
		hulls = 2;
	}
	else if (gr > 500)
	{
		sheet = &spriteSheetT2000;
		hull[0] = gr - 500;
	}

	Uint8 slot[256] = { 0 };
	unsigned int slotsUsed = 0;
	const int xLeft = xCenter - (int)hulls * HULL_W * SCALE / 2;
	for (unsigned int h = 0; h < hulls; ++h)
	{
		Uint8 pixels[HULL_H][HULL_W];
		sprite2x2_decode(*sheet, hull[h], pixels);
		for (int row = 0; row < HULL_H; ++row)
		{
			for (int col = 0; col < HULL_W; ++col)
			{
				const Uint8 c = pixels[row][col];
				if (c == 0)
					continue;
				if (slot[c] == 0 && slotsUsed < COUNTOF(menuUnusedBanks) * 16)
				{
					slot[c] = (Uint8)(menuUnusedBanks[slotsUsed / 16] * 16 + slotsUsed % 16);
					shown[slot[c]] = palettes[0][c];
					++slotsUsed;
				}
				fill_rectangle_wh(VGAScreen, xLeft + ((int)h * HULL_W + col) * SCALE,
				                  yTop + row * SCALE, SCALE, SCALE, (slot[c] != 0) ? slot[c] : c);
			}
		}
	}
}

// Run an options tree; true means the menu launched a full game.
static bool runOptionsMenu(MenuId startMenu);

void setupMenu(void)
{
	runOptionsMenu(MENU_SETUP);
}

bool extraMenu(void)
{
	return runOptionsMenu(MENU_EXTRA);
}

static bool runOptionsMenu(MenuId startMenu)
{

	typedef struct
	{
		const char *header;
		// One slot over the most rows a menu can show, so the terminator always fits.
		const MenuItem items[12];
	} Menu;

	// Shorthands for the two rows every menu ends with, and for the picker columns.
	#define MENU_DONE_ROW  { MENU_ITEM_DONE, "Done", "Return to the previous menu." }, { -1 }
	#define EPISODE_PICKER .getPickerItemsCount = episodeCount, .getPickerItem = episodeItem
	#define SPARK_PICKER   .getPickerItemsCount = sparkModeCount, .getPickerItem = sparkModeItem

	static const Menu menus[] = {
		[MENU_SETUP] = {
			.header = "Setup",
			.items = {
				{ MENU_ITEM_SUBMENU, "Graphics...", "Change the graphics settings.", MENU_GRAPHICS },
				{ MENU_ITEM_SUBMENU, "Sound...", "Change the sound settings.", MENU_SOUND },
				{ MENU_ITEM_SUBMENU, "Enhancements...", "Presets and every change this fork adds to the game.", MENU_ENHANCEMENTS },
				{ MENU_ITEM_SUBMENU, "Diagnostics...", "Debug mode and the online session log.", MENU_DIAGNOSTICS },
				{ MENU_ITEM_SHIP_SENS, SHIP_SENS_NAME, SHIP_SENS_HELP },
#ifdef TOUCH_UI_BUTTONS
				{ MENU_ITEM_TOUCH_OPACITY, "Button Opacity",
				  "How visible the on-screen buttons are; empty hides them." },
#endif
				{ MENU_ITEM_RUMBLE, "Rumble:",
				  "Gamepad vibration for hits on your ship and explosions.",
				  .getPickerItemsCount = rumbleCount, .getPickerItem = rumbleItem },
				{ MENU_ITEM_DONE, "Done", "Return to the main menu." },
				{ -1 }
			},
		},
		[MENU_GRAPHICS] = {
			.header = "Graphics",
			.items = {
#ifndef PLATFORM_HANDHELD
				{ MENU_ITEM_DISPLAY, "Display:", "Change the display mode.",
				  .getPickerItemsCount = getDisplayPickerItemsCount, .getPickerItem = getDisplayPickerItem },
#endif
				{ MENU_ITEM_SCALER, "Scaler:", "Change the pixel art scaling algorithm.",
				  .getPickerItemsCount = getScalerPickerItemsCount, .getPickerItem = getScalerPickerItem },
				{ MENU_ITEM_SCALING_MODE, "Scaling Mode:", "Change the scaling mode.",
				  .getPickerItemsCount = getScalingModePickerItemsCount, .getPickerItem = getScalingModePickerItem },
				{ MENU_ITEM_SMOOTH_MOTION, "Smooth Motion:", "Interpolate motion for smooth high-refresh play." },
				{ MENU_ITEM_SUPERSAMPLE, "Sub-pixel:", "Supersample in-game motion; Native matches your display.",
				  .getPickerItemsCount = getSupersamplePickerItemsCount, .getPickerItem = getSupersamplePickerItem },
#ifdef __vita__
				{ MENU_ITEM_SUBPIXEL_FX, "Smooth FX:", "Update ice, water and lava at the display rate." },
#else
				{ MENU_ITEM_SUBPIXEL_FX, "Sub-pixel FX:", "Ice, water and lava effects at sub-pixel size." },
#endif
				{ MENU_ITEM_RENDER_THREADS, "Render Threads:",
				  "CPU threads for drawing frames; Auto finds the fastest.",
				  .getPickerItemsCount = getRenderThreadsPickerItemsCount,
				  .getPickerItem = getRenderThreadsPickerItem },
				{ MENU_ITEM_VSYNC, "VSync:", "Sync presentation to your monitor's refresh rate." },
				{ MENU_ITEM_FPS, "FPS Cap:", "Cap presented frames; type a number, 0 = uncapped." },
				{ MENU_ITEM_SHOW_FPS, "Show FPS:", "Show a frame-rate counter while playing." },
				MENU_DONE_ROW
			},
		},
		[MENU_SOUND] = {
			.header = "Sound",
			.items = {
				{ MENU_ITEM_MUSIC_VOLUME, "Music Volume", "Change volume with the left/right arrow keys." },
				{ MENU_ITEM_SOUND_VOLUME, "Sound Volume", "Change volume with the left/right arrow keys." },
				{ MENU_ITEM_MUSIC_DEVICE, "Music Synth:", "Synthesizer for music (FluidSynth needs a .sf2).",
				  .getPickerItemsCount = getMusicDeviceItemsCount, .getPickerItem = getMusicDeviceItem },
				{ MENU_ITEM_ARMOR_ALARM, "Armor Alarm:", "Siren while your armor is critically low." },
				{ MENU_ITEM_LINK_SOUNDS, "Link Sounds:", "Sound cue when two ships fuse or unfuse." },
				{ MENU_ITEM_SFX_FILTER, "Sound Filter:",
				  "Soften the 8-bit sound effects with a low-pass filter.",
				  .getPickerItemsCount = sfxFilterCount, .getPickerItem = sfxFilterItem },
				{ MENU_ITEM_SFX_COPY_LIMIT, "Same-Sound Limit:",
				  "How many copies of one sound effect may play at once.",
				  .getPickerItemsCount = sfxCopyLimitCount, .getPickerItem = sfxCopyLimitItem },
				MENU_DONE_ROW
			},
		},
		/* Each enhancement setting belongs to one of six submenus. */
		[MENU_ENHANCEMENTS] = {
			.header = "Enhancements",
			.items = {
				{ MENU_ITEM_ENH_PRESET, "Preset:", "Vanilla behavior, or the Engaged recommended set.",
				  .getPickerItemsCount = enhPresetCount, .getPickerItem = enhPresetItem },
				{ MENU_ITEM_SUBMENU, "Visuals...", "Backgrounds, sparks, and screen effects.", MENU_VISUALS },
				{ MENU_ITEM_SUBMENU, "Heads-Up Display...", "Health bars and the gauges beside your ship.", MENU_HUD },
				{ MENU_ITEM_SUBMENU, "Weapons...", "Restored gear, sidekick autofire, and spark trails.", MENU_WEAPONS },
				{ MENU_ITEM_SUBMENU, "Gameplay...", "Collision, homing, restored enemies, and arcade rules.", MENU_GAMEPLAY },
				{ MENU_ITEM_SUBMENU, "Episode Versions...", "Items that differ between Ep 1-3 and Ep 4-5.", MENU_EPISODE_VERSIONS },
				{ MENU_ITEM_SUBMENU, "Reworks...",
				  "Endless item reworks, for the other modes too.", MENU_REWORKS },
				MENU_DONE_ROW
			},
		},
		[MENU_VISUALS] = {
			.header = "Visuals",
			.items = {
				{ MENU_ITEM_EXTRA_PARALLAX, "Extra Parallax:", "Wider parallax: strafing sweeps the whole map." },
				{ MENU_ITEM_MIRRORED_LAYERS, "Mirrored Layers:", "Over-panned layer edges continue as a mirror." },
				{ MENU_ITEM_EXTRA_SPARKS, "Extra Sparks:", "Denser, longer-lasting explosion spark showers." },
				{ MENU_ITEM_SPECIAL_TINT, "Special Tint:", "Flare specials wash the screen in their colour." },
				{ MENU_ITEM_UNUSED_SPRITES, "Unused Sprites:", "Give spare shop icons to look-alike weapons." },
				{ MENU_ITEM_SUBMENU, "CRT Filter...", "Scanlines, phosphor mask, glow and screen curvature.",
				  MENU_CRT },
				MENU_DONE_ROW
			},
		},
		[MENU_CRT] = {
			.header = "CRT Filter",
			.items = {
				{ MENU_ITEM_CRT_FILTER, "Filter:", "Scanlines, or add a phosphor mask, glow and vignette.",
				  .getPickerItemsCount = crtFilterCount, .getPickerItem = crtFilterItem },
				{ MENU_ITEM_CRT_STRENGTH, "Strength:", "Depth of the scanlines, glow and phosphor mask.",
				  .getPickerItemsCount = crtStrengthCount, .getPickerItem = crtStrengthItem },
				{ MENU_ITEM_CRT_MASK, "Mask:",
				  "Phosphor pattern in Full: aperture grille or slots.",
				  .getPickerItemsCount = crtMaskCount, .getPickerItem = crtMaskItem },
				{ MENU_ITEM_CRT_CURVATURE, "Curvature:", "Bend the picture like a curved tube screen.",
				  .getPickerItemsCount = crtCurvatureCount, .getPickerItem = crtCurvatureItem },
				{ MENU_ITEM_CRT_CORNERS, "Corners:",
				  "Round the picture's corners like a tube screen.",
				  .getPickerItemsCount = crtCornerCount, .getPickerItem = crtCornerItem },
				{ MENU_ITEM_CRT_ROLLING_BAR, "Rolling Bar:",
				  "A faint band that rolls down the screen now and then.",
				  .getPickerItemsCount = crtRollingBarCount, .getPickerItem = crtRollingBarItem },
				MENU_DONE_ROW
			},
		},
		[MENU_HUD] = {
			.header = "Heads-Up Display",
			.items = {
				{ MENU_ITEM_SUBMENU, "Enemy Bars...", "Health bars on enemies you have damaged.", MENU_ENEMY_BARS },
				{ MENU_ITEM_SUBMENU, "Boss Bars...", "Style and placement of the boss health bars.", MENU_BOSS_BARS },
				{ MENU_ITEM_SUBMENU, "Gauges...", "Gradient and damage flash of the three gauges.", MENU_GAUGES },
				{MENU_ITEM_BOSS_VULN_CUE, "Vulnerable Cue:", "Which hulls flash when they turn damageable.",
				  .getPickerItemsCount = vulnCueCount, .getPickerItem = vulnCueItem},
#ifdef TOUCH_UI_BUTTONS
				{ MENU_ITEM_TOUCH_SIDEKICKS, "Sidekick Buttons:", "On-screen buttons that fire your sidekicks." },
				{ MENU_ITEM_TOUCH_NAV, "Nav Buttons:",
				  "Cursor keys and confirm on every menu, not just lists." },
#endif
				MENU_DONE_ROW
			},
		},
		[MENU_ENEMY_BARS] = {
			.header = "Enemy Bars",
			.items = {
				{ MENU_ITEM_ENEMY_BARS, "Show Bars:", "Show a small health bar on enemies you have hit." },
				{ MENU_ITEM_ENEMY_BAR_LAYOUT, "Layout:", "Horizontal or vertical bar.",
				  .getPickerItemsCount = enemyBarLayoutCount, .getPickerItem = enemyBarLayoutItem },
				{ MENU_ITEM_ENEMY_BAR_POS, "Position:", "Where the bar sits relative to the enemy.",
				  .getPickerItemsCount = enemyBarPositionCount, .getPickerItem = enemyBarPositionItem },
				{ MENU_ITEM_ENEMY_BAR_OPACITY, "Opacity:", "Bar transparency. Left/right to adjust." },
				MENU_DONE_ROW
			},
		},
		[MENU_BOSS_BARS] = {
			.header = "Boss Bars",
			.items = {
				{ MENU_ITEM_BOSS_BAR_STYLE, "Style:", "Classic or redesigned boss health bars.",
				  .getPickerItemsCount = bossBarStyleCount, .getPickerItem = bossBarStyleItem },
				{ MENU_ITEM_BOSS_BAR_LAYOUT, "Layout:", "Top/Bottom (horizontal) or Left/Right (vertical).",
				  .getPickerItemsCount = bossBarLayoutCount, .getPickerItem = bossBarLayoutItem },
				{ MENU_ITEM_BOSS_BAR_TWO, "Two Bars:", "How two boss bars are arranged.",
				  .getPickerItemsCount = bossBarTwoCount, .getPickerItem = bossBarTwoItem },
				MENU_DONE_ROW
			},
		},
		[MENU_GAUGES] = {
			.header = "Gauges",
			.items = {
				{ MENU_ITEM_GAUGE_GRAD_GEN, "Generator:", "Generator gauge gradient direction (bright end).",
				  .getPickerItemsCount = gaugeGradCount, .getPickerItem = gaugeGradItem },
				{ MENU_ITEM_GAUGE_GRAD_SHIELD, "Shield:", "Shield gauge gradient direction (bright end).",
				  .getPickerItemsCount = gaugeGradCount, .getPickerItem = gaugeGradItem },
				{ MENU_ITEM_GAUGE_GRAD_ARMOR, "Armor:", "Armor gauge gradient direction (bright end).",
				  .getPickerItemsCount = gaugeGradCount, .getPickerItem = gaugeGradItem },
				{ MENU_ITEM_GAUGE_FLASH_SHIELD, "Shield Flash:", "Flash the shield gauge white when it takes damage." },
				{ MENU_ITEM_GAUGE_FLASH_ARMOR, "Armor Flash:", "Flash the armor gauge white when it takes damage." },
				MENU_DONE_ROW
			},
		},
		[MENU_WEAPONS] = {
			.header = "Weapons",
			.items = {
				{ MENU_ITEM_CHARGE_LASER, "Charge-Laser:", "Re-add the cut DOS charge sidekick to its shops." },
				{ MENU_ITEM_SIDEKICK_AUTOFIRE, "Sidekick Autofire:", "Charge sidekicks autofire on the held fire button." },
				{ MENU_ITEM_SUBMENU, "Spark Trails...", "Weapons whose spark trails differ per episode.", MENU_SPARK_TRAILS },
				MENU_DONE_ROW
			},
		},
		[MENU_SPARK_TRAILS] = {
			// Only the Tyrian 2000 (ep4/5) item data gives these four a superspark projectile
			// trail. Auto plays each episode as it shipped; On and Off force it everywhere.
			.header = "Spark Trails",
			.items = {
				{ MENU_ITEM_SPARKS_MODE_BASE + SSW_MEGA_PULSE, "Mega Pulse:",
				  "Spark trail on the Mega Pulse front gun.", SPARK_PICKER },
				{ MENU_ITEM_SPARKS_MODE_BASE + SSW_WALLOP_BEAM, "Wallop Beam:",
				  "Spark trail on the Beno Wallop Beam sidekick.", SPARK_PICKER },
				{ MENU_ITEM_SPARKS_MODE_BASE + SSW_PROTRON_B, "Protron -B-:",
				  "Spark trail on the Beno Protron -B- sidekick.", SPARK_PICKER },
				{ MENU_ITEM_SPARKS_MODE_BASE + SSW_ICE, "Ice Beam:",
				  "Spark trail on the Ice Beam and Blast specials.", SPARK_PICKER },
				{ MENU_ITEM_WALLOP_BOLT, "Wallop 2nd Bolt:",
				  "Ep4/5 second bolt per volley: Auto, On, or Off.", SPARK_PICKER },
				{ MENU_ITEM_SUBMENU, "Spark Caps...", "Hold a trail to the classic limit despite Extra Sparks.", MENU_SPARK_CAPS },
				MENU_DONE_ROW
			},
		},
		[MENU_SPARK_CAPS] = {
			.header = "Spark Caps",
			.items = {
				{ MENU_ITEM_SPARKS_CAP_BASE + SSW_MEGA_PULSE, "Mega Pulse:", "Classic spark limit for the Mega Pulse trail." },
				{ MENU_ITEM_SPARKS_CAP_BASE + SSW_WALLOP_BEAM, "Wallop Beam:", "Classic spark limit for the Wallop Beam trail." },
				{ MENU_ITEM_SPARKS_CAP_BASE + SSW_PROTRON_B, "Protron -B-:", "Classic spark limit for the Protron -B- trail." },
				{ MENU_ITEM_SPARKS_CAP_BASE + SSW_ICE, "Ice Beam:", "Classic spark limit for the Ice Beam trail." },
				MENU_DONE_ROW
			},
		},
		[MENU_GAMEPLAY] = {
			.header = "Gameplay",
			.items = {
				{ MENU_ITEM_SHOT_HITBOXES, "Shot Hitboxes:", "Where a shot hits from: its middle or its corner." },
				{ MENU_ITEM_GUIDED_AIM, "Guided Aim:", "Guided shots steer to where enemies are drawn." },
				{ MENU_ITEM_BASE_DISPENSERS, "Ice Base Shots:", "Wake dormant ice bases in the main game." },
				// Hidden with Clear while no custom episodes exist.
				{ MENU_ITEM_CUSTOM_ENDLESS, "Custom Endless:", "Offline Endless draws levels from custom episodes.",
				  .getPickerItemsCount = customEndlessCount, .getPickerItem = customEndlessItem },
				{ MENU_ITEM_CLEAR_CLV, "Clear .clv", "Delete every custom episode file and its folder." },
				{ MENU_ITEM_SUBMENU, "Arcade Modes...", "Tweaks for the arcade and Super Arcade modes.", MENU_ARCADE_MODES },
				MENU_DONE_ROW
			},
		},
		[MENU_ARCADE_MODES] = {
			// Rear Gun Scale skips the linked pair alone: player two's rear bay is its life
			// counter there (arcade_rear_scale_active). All three bind the session online.
			.header = "Arcade Modes",
			.items = {
				{ MENU_ITEM_ARCADE_LIFE_BOOST, "Life Boost:", "Arcade lives raise your shield and armor caps." },
				{ MENU_ITEM_ARCADE_RANDOM_BALLS, "Random Pickups:", "Randomize the weapon each pickup ball gives." },
				{ MENU_ITEM_ARCADE_REAR_SCALE, "Rear Gun Scale:", "Rear gun power rises with your life count too." },
				MENU_DONE_ROW
			},
		},
		[MENU_EPISODE_VERSIONS] = {
			// Items whose ep1-3 and ep4/5 data differ beyond the spark trail. Auto plays each
			// episode as it shipped; the other two force one version everywhere.
			.header = "Episode Versions",
			.items = {
				{ MENU_ITEM_SUBMENU, "Firing Sounds...", "Weapons whose firing sound differs per episode.", MENU_FIRING_SOUNDS },
				{ MENU_ITEM_SUBMENU, "Shop Pictures...", "Shop artwork that differs between the two sets.", MENU_SHOP_PICTURES },
				{ MENU_ITEM_ZICA_BASE, "Zica Laser Lv11:",
				  "Ep1-3 columns vs Ep4-5 spread. Endless Auto: Ep4-5.", EPISODE_PICKER },
				{ MENU_ITEM_EPDIFF_BASE + EDW_XEGA_BALL, "Xega Ball:",
				  "Ep1-3 two weak balls vs Ep4-5 one strong ball.", EPISODE_PICKER },
				{ MENU_ITEM_EPDIFF_BASE + EDW_MICROSOL_OPT5, "MicroSol Opt 5:",
				  "Ep1-3 8-way fan vs Ep4-5 twin shot (MicroSol ship).", EPISODE_PICKER },
				{ MENU_ITEM_EPDIFF_BASE + EDW_FLARE, "Flare Blast:",
				  "Which episode's blast sprite the Flare uses.", EPISODE_PICKER },
				MENU_DONE_ROW
			},
		},
		[MENU_FIRING_SOUNDS] = {
			// These five differ between the two item sets in nothing but their firing sound.
			.header = "Firing Sounds",
			.items = {
				{ MENU_ITEM_EPDIFF_BASE + EDW_NEEDLE_LASER, "Needle Laser:",
				  "Which episode's firing sound the Needle Laser uses.", EPISODE_PICKER },
				{ MENU_ITEM_EPDIFF_BASE + EDW_BUBBLE_GUM, "Bubble Gum-Gun:",
				  "Which episode's firing sound the Gum-Gun uses.", EPISODE_PICKER },
				{ MENU_ITEM_EPDIFF_BASE + EDW_FLYING_PUNCH, "Flying Punch:",
				  "Which episode's firing sound the Flying Punch uses.", EPISODE_PICKER },
				{ MENU_ITEM_EPDIFF_BASE + EDW_PRETZEL_MISSILE, "Pretzel Missile:",
				  "Which episode's firing sound the Pretzel uses.", EPISODE_PICKER },
				{ MENU_ITEM_EPDIFF_BASE + EDW_DRAGON_FROST, "Dragon Frost:",
				  "Which episode's firing sound the Dragon Frost uses.", EPISODE_PICKER },
				MENU_DONE_ROW
			},
		},
		[MENU_SHOP_PICTURES] = {
			// The three items the two sets differ on in shop artwork and nothing else.
			.header = "Shop Pictures",
			.items = {
				{ MENU_ITEM_EPDIFF_BASE + EDW_SOLAR_SHIELD, "Solar Shield Icon:",
				  "Ep1-3 shop icon (MicroCorp) vs Ep4-5 (Gencore).", EPISODE_PICKER },
				{ MENU_ITEM_EPDIFF_BASE + EDW_USHIP_PIC, "U-Ship Picture:",
				  "Ep1-3 Gencore hull vs Ep4-5 USP delta in the shop.", EPISODE_PICKER },
				{ MENU_ITEM_EPDIFF_BASE + EDW_NORTSHIP_PIC, "Nort Ship Picture:",
				  "Ep1-3 Stalker hull vs Ep4-5 USP delta in the shop.", EPISODE_PICKER },
				MENU_DONE_ROW
			},
		},
		[MENU_REWORKS] = {
			.header = "Reworks",
			.items = {
				{ MENU_ITEM_REWORK_BASE + REWORK_ZICA_BUFF, "Zica Laser Buff:",
				  "Zica Laser Lv11 also fires the Lv10 beam." },
				{ MENU_ITEM_REWORK_BASE + REWORK_ATTRACTOR, "Attractor:",
				  "Hauls pickups to your ship until they are collected." },
				{ MENU_ITEM_REWORK_BASE + REWORK_ORANGE_SHIELD, "Orange Shield:",
				  "The shield ring circles your ship, not above it." },
				{ MENU_ITEM_REWORK_BASE + REWORK_RAMMING, "Ramming:",
				  "Ram kills pay as kills; you ram while invulnerable." },
				{ MENU_ITEM_REWORK_BASE + REWORK_ITEM_NAMES, "Item Names:",
				  "Endless names, such as Microbomb and Pearl Shot." },
				MENU_DONE_ROW
			},
		},
		[MENU_DIAGNOSTICS] = {
			.header = "Diagnostics",
			.items = {
				{ MENU_ITEM_DEBUG_MODE, "Debug Mode:", "Enable the debug menu and debug level select." },
				{ MENU_ITEM_NET_LOG, "Network Log:", "Record online sessions to a net log file." },
#ifdef PLATFORM_HANDHELD
				// These platforms need an in-game way to clear crash and network logs.
				{ MENU_ITEM_CLEAR_LOGS, "Clear Logs", "Delete every log file saved on this system." },
#endif
				MENU_DONE_ROW
			},
		},
		[MENU_EXTRA] = {
			.header = "Extra",
			.items = {
				{ MENU_ITEM_INSTRUCTIONS, "Instructions", "Read the instructions and help topics." },
				{ MENU_ITEM_JUKEBOX, "Jukebox", "Listen to the music of Tyrian." },
				{ MENU_ITEM_SUBMENU, "Secret Modes...", "Play Destruct, SuperTyrian, or Super Arcade.", MENU_SECRET_MODES },
				{ MENU_ITEM_SHIP_EDITOR, "Ship Editor...", "Design the Tab+Number custom ships." },
				{ MENU_ITEM_WEAPON_EDITOR, "Weapon Editor...", "Design your own weapon with a live preview." },
				{ MENU_ITEM_SUBMENU, "Transfer...", "Copy saves or custom creations to another device.", MENU_TRANSFER },
				{ MENU_ITEM_XMAS, "Christmas Mode:", "Festive graphics and voices." },
				{ MENU_ITEM_DONE, "Done", "Return to the main menu." },
				{ -1 }
			},
		},
		[MENU_SECRET_MODES] = {
			.header = "Secret Modes",
			.items = {
				{ MENU_ITEM_DESTRUCT, "Destruct", "Play the secret Destruct mini-game." },
				{ MENU_ITEM_SUPERTYRIAN, "SuperTyrian", "Play the tougher SuperTyrian mode." },
				{ MENU_ITEM_SUBMENU, "Super Arcade...", "Play as one of the secret Super Arcade ships.", MENU_ARCADE },
				{ MENU_ITEM_SUBMENU, "Command Line...", "Toggle the command-line cheat options.", MENU_CMDLINE },
				MENU_DONE_ROW
			},
		},
		[MENU_TRANSFER] = {
			.header = "Transfer",
			.items = {
				{ MENU_ITEM_SUBMENU, "Save...", "Copy one save slot.", MENU_TRANSFER_SAVE },
				{ MENU_ITEM_SUBMENU, "All Saves...", "Copy every save slot.", MENU_TRANSFER_SAVES },
				{ MENU_ITEM_SUBMENU, "Custom Ships...", "Replace only the compiled custom ships.", MENU_TRANSFER_SHIPS },
				{ MENU_ITEM_SUBMENU, "Custom Weapons...", "Replace only the complete custom-weapon library.", MENU_TRANSFER_WEAPONS },
				// Hidden when no valid custom episode is installed.
				{ MENU_ITEM_SUBMENU, "Custom Levels...", "Copy the installed custom episodes.", MENU_TRANSFER_LEVELS },
				{ MENU_ITEM_SUBMENU, "Custom Data...", "Copy custom ships and the complete weapon library.", MENU_TRANSFER_CUSTOM },
				{ MENU_ITEM_SUBMENU, "Transfer All...", "Replace every save and all custom creations at once.", MENU_TRANSFER_ALL },
				MENU_DONE_ROW
			},
		},
		[MENU_TRANSFER_SAVE] = {
			.header = "Save Transfer",
			.items = {
				{ MENU_ITEM_TRANSFER_SAVE_UPLOAD, "Upload", "Choose a save slot to send." },
				{ MENU_ITEM_TRANSFER_SAVE_DOWNLOAD, "Download", "Receive a save, then choose its destination slot." },
				MENU_DONE_ROW
			},
		},
		[MENU_TRANSFER_SAVES] = {
			.header = "All Saves Transfer",
			.items = {
				{ MENU_ITEM_TRANSFER_SAVES_UPLOAD, "Upload", "Send every save slot." },
				{ MENU_ITEM_TRANSFER_SAVES_DOWNLOAD, "Download", "Replace every save slot." },
				MENU_DONE_ROW
			},
		},
		[MENU_TRANSFER_SHIPS] = {
			.header = "Custom Ships Transfer",
			.items = {
				{ MENU_ITEM_TRANSFER_SHIPS_UPLOAD, "Upload", "Send only the compiled custom ships." },
				{ MENU_ITEM_TRANSFER_SHIPS_DOWNLOAD, "Download", "Replace only the receiver's custom ships." },
				MENU_DONE_ROW
			},
		},
		[MENU_TRANSFER_WEAPONS] = {
			.header = "Custom Weapons Transfer",
			.items = {
				{ MENU_ITEM_TRANSFER_WEAPONS_UPLOAD, "Upload", "Send only the complete custom-weapon library." },
				{ MENU_ITEM_TRANSFER_WEAPONS_DOWNLOAD, "Download", "Replace only the receiver's custom weapons." },
				MENU_DONE_ROW
			},
		},
		[MENU_TRANSFER_LEVELS] = {
			.header = "Custom Levels Transfer",
			.items = {
				{ MENU_ITEM_TRANSFER_LEVELS_UPLOAD, "Upload", "Send every installed custom episode." },
				{ MENU_ITEM_TRANSFER_LEVELS_DOWNLOAD, "Download", "Add the sender's custom episodes to this device." },
				MENU_DONE_ROW
			},
		},
		[MENU_TRANSFER_CUSTOM] = {
			.header = "Custom Data Transfer",
			.items = {
				{ MENU_ITEM_TRANSFER_CUSTOM_UPLOAD, "Upload", "Send custom ships and every custom weapon design." },
				{ MENU_ITEM_TRANSFER_CUSTOM_DOWNLOAD, "Download", "Replace custom ships and weapons with received data." },
				MENU_DONE_ROW
			},
		},
		[MENU_TRANSFER_ALL] = {
			.header = "Transfer All",
			.items = {
				{ MENU_ITEM_TRANSFER_ALL_UPLOAD, "Upload", "Send every save slot, custom ship, and custom weapon." },
				{ MENU_ITEM_TRANSFER_ALL_DOWNLOAD, "Download", "Replace all saves and custom data without prompting." },
				MENU_DONE_ROW
			},
		},
		[MENU_ARCADE] = {
			.header = "Super Arcade",
			.items = {
				// name = the title-screen code (specialName[i]); the ship name
				// (superShips[i+1]) is the description shown at the bottom of the screen.
				{ MENU_ITEM_ARCADE_SHIP_BASE + 0, specialName[0], superShips[1] },
				{ MENU_ITEM_ARCADE_SHIP_BASE + 1, specialName[1], superShips[2] },
				{ MENU_ITEM_ARCADE_SHIP_BASE + 2, specialName[2], superShips[3] },
				{ MENU_ITEM_ARCADE_SHIP_BASE + 3, specialName[3], superShips[4] },
				{ MENU_ITEM_ARCADE_SHIP_BASE + 4, specialName[4], superShips[5] },
				{ MENU_ITEM_ARCADE_SHIP_BASE + 5, specialName[5], superShips[6] },
				{ MENU_ITEM_ARCADE_SHIP_BASE + 6, specialName[6], superShips[7] },
				{ MENU_ITEM_ARCADE_SHIP_BASE + 7, specialName[7], superShips[8] },
				{ MENU_ITEM_ARCADE_SHIP_BASE + 8, specialName[8], superShips[9] },
				MENU_DONE_ROW
			},
		},
		[MENU_CMDLINE] = {
			.header = "Command Line",
			.items = {
				{ MENU_ITEM_RICH_MODE, "Rich Mode:", "Start a new game with maximum money (LOOT)." },
				{ MENU_ITEM_CONSTANT_PLAY, "Constant Play:", "Testing mode; the C key toggles invincibility (CONSTANT)." },
				{ MENU_ITEM_CONSTANT_DIE, "Constant Die:", "Testing mode: ship constantly self-destructs (DEATH)." },
				MENU_DONE_ROW
			},
		},
	};

	#undef MENU_DONE_ROW
	#undef EPISODE_PICKER
	#undef SPARK_PICKER

	char buffer[100];

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	bool restart = true;

	MenuId menuParents[COUNTOF(menus)] = { MENU_NONE };
	size_t selectedMenuItemIndexes[COUNTOF(menus)] = { 0 };
	MenuId currentMenu = startMenu;
	MenuItemId currentPicker = MENU_ITEM_NONE;
	size_t pickerSelectedIndex = 0;
	bool fpsTyped = false;  // an FPS cap is being typed digit-by-digit (desktop keyboard)

	// What the last "Clear Logs" press did, reported in that row's value column until
	// the menu is left: nothing yet, logs deleted, or none there to delete.
	enum { LOGS_CLEAR_UNTOUCHED, LOGS_CLEAR_DONE, LOGS_CLEAR_ABSENT } logsCleared = LOGS_CLEAR_UNTOUCHED;

	/* See comment in JE_helpSystem regarding the virtual screen width. */
	const int xCenter = 320 / 2;
	const int yMenuHeader = 4;
	const int wMenuItemName = 135;
	const int wMenuItemValue = 95;
	const int wMenuItem = wMenuItemName + wMenuItemValue;
	const int xMenuItem = xCenter - wMenuItem / 2;
	const int xMenuItemName = xMenuItem;
	const int xMenuItemValue = xMenuItemName + wMenuItemName;
	int yMenuItems = 37;   // first row; raised for a menu whose rows have to be compressed
	int dyMenuItems = 21;  // row pitch; compressed when a menu has many rows (see below)
	const int hMenuItem = 13;

	for (; ; )
	{
		if (restart)
		{
			JE_loadPic(VGAScreen2, 2, false);
			fill_rectangle_wh(VGAScreen2, 0, 192, vga_width, 8, 0);
		}

		// Restore background.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Committed typed caps snap to the arrows' 5fps floor: below that the paced
		// menus themselves become nearly unusable.
		if (!fpsTyped && fps_cap > 0 && fps_cap < 5)
			set_fps(5);

		const Menu *menu = &menus[currentMenu];

		/* Watch the enhancement settings rather than every row that can write one: any hand
		 * edit lands here as a set matching neither preset, and becomes the set Custom holds. */
		enhancementNoteCustom();

		// Draw header.
		draw_font_hv_shadow(VGAScreen, xCenter, yMenuHeader, menu->header, large_font, centered, 15, -3, false, 2);

		int yPicker = 0;
		const int dyPickerItem = 15;
		const int dyPickerItemPadding = 2;
		const int hPickerItem = dyPickerItem - dyPickerItemPadding;

		size_t *const selectedMenuItemIndex = &selectedMenuItemIndexes[currentMenu];

		// Drop this frame's conditional rows (isMenuItemVisible) up front, so everything below
		// indexes one contiguous list of rows that are actually on screen.
		MenuItem visibleItems[COUNTOF(menu->items)];
		size_t menuItemsCount = 0;
		for (size_t i = 0; i + 1 < COUNTOF(visibleItems) && menu->items[i].id != (MenuItemId)-1; ++i)
			if (isMenuItemVisible(&menu->items[i]))
				visibleItems[menuItemsCount++] = menu->items[i];
		visibleItems[menuItemsCount].id = (MenuItemId)-1;

		const MenuItem *menuItems = visibleItems;

		// A row can vanish under the cursor if isMenuItemVisible ever hides one again,
		// so keep the selection inside the list.
		if (*selectedMenuItemIndex >= menuItemsCount)
			*selectedMenuItemIndex = menuItemsCount - 1;
		if (currentPicker != MENU_ITEM_NONE)
		{
			size_t pickerRow = 0;
			while (pickerRow < menuItemsCount && menuItems[pickerRow].id != currentPicker)
				++pickerRow;
			if (pickerRow < menuItemsCount)
				*selectedMenuItemIndex = pickerRow;
			else
				currentPicker = MENU_ITEM_NONE;
		}

		// Tighten long menus enough to keep the final row above the footer, using
		// the spare gap below the header first.
		yMenuItems = 37;
		dyMenuItems = 21;
		if (menuItemsCount > 1 && yMenuItems + dyMenuItems * (int)(menuItemsCount - 1) > 172)
		{
			yMenuItems = 30;
			dyMenuItems = (172 - yMenuItems) / (int)(menuItemsCount - 1);
		}

		// Draw menu items.

		for (size_t i = 0; i < menuItemsCount; ++i)
		{
			const MenuItem *const menuItem = &menuItems[i];

			const int y = yMenuItems + dyMenuItems * i;

			const bool selected = i == *selectedMenuItemIndex;
			const bool disabled = (currentPicker != MENU_ITEM_NONE && !selected)
			                   || (menuItem->id == MENU_ITEM_SUPERSAMPLE && !smoothMotion)
			                   || (menuItem->id == MENU_ITEM_SUBPIXEL_FX && !subpixelFxAvailable())
			                   || (menuItem->id == MENU_ITEM_CRT_STRENGTH
			                       && crt_filter == CRT_FILTER_OFF)
			                   || (menuItem->id == MENU_ITEM_CRT_MASK
			                       && crt_filter != CRT_FILTER_FULL);

			if (selected)
				yPicker = y;

			// The name and value share one brightness: dimmed while another row's picker
			// is open, brightened on the selected row.
			const int shade = -3 + (selected ? 2 : 0) + (disabled ? -4 : 0);

			draw_font_hv_shadow(VGAScreen, xMenuItemName, y, menuItem->name, normal_font, left_aligned, 15, shade, false, 2);

			/* Rows select value text here; custom widgets and empty values leave it NULL. */
			const char *value = NULL;

			const int *const intSetting = menuItemIntSetting(menuItem->id);
			const bool *const boolSetting = menuItemBoolSetting(menuItem->id);

			if (intSetting != NULL && menuItemHasPicker(menuItem))
				value = menuItem->getPickerItem((size_t)*intSetting, buffer, sizeof buffer);
			else if (boolSetting != NULL)
			{
				bool shown = *boolSetting;
				if (menuItem->id == MENU_ITEM_SUBPIXEL_FX && !subpixelFxAvailable())
					shown = false;
				value = shown ? "On" : "Off";
			}
			else switch (menuItem->id)
			{
			case MENU_ITEM_ENH_PRESET:
				value = enhancementPresetName(enhancementPresetState());
				break;

			case MENU_ITEM_DISPLAY:
				value = "Window";
				if (fullscreen_display >= 0)
				{
					snprintf(buffer, sizeof(buffer), "Display %d", fullscreen_display + 1);
					value = buffer;
				}
				break;

			case MENU_ITEM_SCALER:
				value = scalers[scaler].name;
				break;

			case MENU_ITEM_SCALING_MODE:
				value = scaling_mode_names[scaling_mode];
				break;

			case MENU_ITEM_SUPERSAMPLE:
				// Auto and Native both resolve at present time; show what they land on.
				if (render_supersample == 0 || render_supersample == RENDER_SUPERSAMPLE_NATIVE)
					snprintf(buffer, sizeof(buffer), "%s (%dx)",
					         supersampleNames[render_supersample], effective_supersample());
				else
					snprintf(buffer, sizeof(buffer), "%s", supersampleNames[render_supersample]);
				value = buffer;
				break;

			case MENU_ITEM_RENDER_THREADS:
				if (render_threads == 0)
				{
					value = "Auto";
				}
				else
				{
					snprintf(buffer, sizeof(buffer), "%d", render_threads);
					value = buffer;
				}
				break;

			case MENU_ITEM_SMOOTH_MOTION:
				value = smoothMotion ? "On" : "Off";
				break;

			case MENU_ITEM_VSYNC:
				value = output_vsync ? "On" : "Off";
				break;

			case MENU_ITEM_FPS:
				if (fpsTyped)
					snprintf(buffer, sizeof(buffer), "%d_", fps_cap);
				else if (fps_cap == 0)
					snprintf(buffer, sizeof(buffer), "Uncapped");
				else
					snprintf(buffer, sizeof(buffer), "%d", fps_cap);
				value = buffer;
				break;

			case MENU_ITEM_MUSIC_DEVICE:
				value = music_device_names[music_device];
				break;

			case MENU_ITEM_NET_LOG:
				value = crashlog_get_netlog_enabled() ? "On" : "Off";
				break;

			case MENU_ITEM_CLEAR_LOGS:
				// An action row, so it has no value of its own; the column carries the
				// outcome of the press instead, and stays blank until there is one.
				if (logsCleared != LOGS_CLEAR_UNTOUCHED)
					value = logsCleared == LOGS_CLEAR_DONE ? "Cleared" : "No Logs";
				break;

			case MENU_ITEM_ENEMY_BAR_OPACITY:
				snprintf(buffer, sizeof(buffer), "%d%%", enemyBarOpacity);
				value = buffer;
				break;

			case MENU_ITEM_UNUSED_SPRITES:
				value = unusedShopSprites ? "On" : "Off";
				break;

			case MENU_ITEM_XMAS:
				value = xmas ? "On" : "Off";
				break;

			case MENU_ITEM_SHOT_HITBOXES:
				value = centeredShotHitboxes ? "Centered" : "Classic";
				break;

			case MENU_ITEM_SIDEKICK_AUTOFIRE:
			{
				// Off/On/Charged are the reachable modes; "Fast" only shows if the
				// debug menu set CHARGE_AUTOFIRE_FAST; it can't be selected here.
				static const char *const names[CHARGE_AUTOFIRE_NUM] = { "Off", "On", "Charged", "Fast" };
				value = names[chargeSidekickAutofire % CHARGE_AUTOFIRE_NUM];
				break;
			}

			case MENU_ITEM_MUSIC_VOLUME:
				JE_barDrawShadow(VGAScreen, xMenuItemValue, y, 1, music_disabled ? 170 : 174, (tyrMusicVolume + 4) / 8, 2, 10);
				JE_rectangle(VGAScreen, xMenuItemValue - 2, y - 2, xMenuItemValue + 96, y + 11, 242);
				break;

			case MENU_ITEM_SOUND_VOLUME:
				JE_barDrawShadow(VGAScreen, xMenuItemValue, y, 1, samples_disabled ? 170 : 174, (fxVolume + 4) / 8, 2, 10);
				JE_rectangle(VGAScreen, xMenuItemValue - 2, y - 2, xMenuItemValue + 96, y + 11, 242);
				break;

			case MENU_ITEM_SHIP_SENS:
			{
				// The middle marker is the classic 1:1 feel; compare drawn bar counts.
				const int amt = (ship_sensitivity + 4) / 8;
				const int mark = (SHIP_SENS_DEFAULT + 4) / 8;
				JE_barDrawShadow(VGAScreen, xMenuItemValue, y, 1, 174, amt, 2, 10);
				JE_barDrawMark(VGAScreen, xMenuItemValue, y,
				               amt >= mark ? SHIP_SENS_MARK_COL : SHIP_SENS_MARK_COL_DIM, mark, 2, 10);
				JE_rectangle(VGAScreen, xMenuItemValue - 2, y - 2, xMenuItemValue + 96, y + 11, 242);
				break;
			}

			case MENU_ITEM_TOUCH_OPACITY:
			{
				// Opacity uses the full bar and has no neutral marker.
				const int bars = (wMenuItemValue + 1) / 3;   // segments are 2 px plus a 1 px gap
				const int amt = (touchButtonOpacity * bars + TOUCH_OPACITY_MAX / 2) / TOUCH_OPACITY_MAX;
				JE_barDrawShadow(VGAScreen, xMenuItemValue, y, 1, 174, amt, 2, 10);
				JE_rectangle(VGAScreen, xMenuItemValue - 2, y - 2, xMenuItemValue + 96, y + 11, 242);
				break;
			}

			default:
				// Submenu rows, headings, Done, and the Super Arcade ship codes carry no value.
				break;
			}

			if (value != NULL)
				draw_font_hv_shadow(VGAScreen, xMenuItemValue, y, value, normal_font, left_aligned, 15, shade, false, 2);
		}

		Palette shown;
		memcpy(shown, colors, sizeof(shown));
		const MenuItemId hovered = menuItems[*selectedMenuItemIndex].id;
		if (currentMenu == MENU_ARCADE && hovered >= MENU_ITEM_ARCADE_SHIP_BASE &&
		    hovered < MENU_ITEM_ARCADE_SHIP_BASE + SA)
		{
			if (weaponPort[1].name[0] == '\0')
				JE_loadItemDat();
			const int yList =
			    yMenuItems + (dyMenuItems * (int)(menuItemsCount - 1) + hMenuItem) / 2;
			drawArcadeShip((unsigned int)(hovered - MENU_ITEM_ARCADE_SHIP_BASE),
			               xMenuItemValue + wMenuItemValue / 2, yList - 28, shown);
		}

		// Draw status text. The Music Synth row shows live SoundFont status so the
		// user can confirm FluidSynth actually picked up their .sf2 (see audio.c).
		const char *statusText = menuItems[*selectedMenuItemIndex].description;
		char musicSynthStatus[128];
		if (menuItems[*selectedMenuItemIndex].id == MENU_ITEM_ENH_PRESET)
		{
			// Say what the current state means. A preset rewrites every enhancement setting,
			// so it also says that Custom is kept and can be picked again.
			switch (enhancementPresetState())
			{
			case ENH_PRESET_VANILLA:
				statusText = "Every enhancement set to play like the original game.";
				break;
			case ENH_PRESET_ENGAGED:
				statusText = "Every enhancement at this fork's recommended value.";
				break;
			default:
				statusText = "Your own mix, kept as Custom while you try the others.";
				break;
			}
		}
		else if (menuItems[*selectedMenuItemIndex].id == MENU_ITEM_MUSIC_DEVICE)
		{
			const bool noSoundFont = !soundfont_available();
			if (noSoundFont && (music_device == FLUIDSYNTH || currentPicker == MENU_ITEM_MUSIC_DEVICE))
			{
				// Says what would un-gray the FluidSynth entry the picker is showing.
				statusText = "FluidSynth needs a .sf2 next to the game or in data.";
			}
			else if (music_device == FLUIDSYNTH)
			{
				if (midi_soundfont_loaded)
					// %.34s caps a long filename so the one-line status stays on-screen (~54 chars).
					snprintf(musicSynthStatus, sizeof(musicSynthStatus), "SoundFont loaded: %.34s", soundfont_basename());
				else
					snprintf(musicSynthStatus, sizeof(musicSynthStatus), "SoundFont failed to load -- try a different .sf2.");
				statusText = musicSynthStatus;
			}
			else if (music_device == NATIVE_MIDI)
			{
				statusText = "Native OS synth -- ignores custom SoundFonts.";
			}
		}
		JE_textShade(VGAScreen, xMenuItemName, 190, statusText, 15, 4, PART_SHADE);

		// Draw picker box and items.

		if (currentPicker != MENU_ITEM_NONE)
		{
			const MenuItem *selectedMenuItem = &menuItems[*selectedMenuItemIndex];
			const size_t pickerItemsCount = selectedMenuItem->getPickerItemsCount();

			const int hPicker = dyPickerItem * pickerItemsCount - dyPickerItemPadding;
			yPicker = MIN(yPicker, 200 - 10 - (hPicker + 5 + 2));

			JE_rectangle(VGAScreen, xMenuItemValue - 5, yPicker- 3, xMenuItemValue + wMenuItemValue + 5 - 1, yPicker + hPicker + 3 - 1, 248);
			JE_rectangle(VGAScreen, xMenuItemValue - 4, yPicker- 4, xMenuItemValue + wMenuItemValue + 4 - 1, yPicker + hPicker + 4 - 1, 250);
			JE_rectangle(VGAScreen, xMenuItemValue - 3, yPicker- 5, xMenuItemValue + wMenuItemValue + 3 - 1, yPicker + hPicker + 5 - 1, 248);
			fill_rectangle_wh(VGAScreen, xMenuItemValue - 2, yPicker - 2, wMenuItemValue + 2 + 2, hPicker + 2 + 2, 224);

			for (size_t i = 0; i < pickerItemsCount; ++i)
			{
				const int y = yPicker + dyPickerItem * (int)i;

				const bool selected = i == pickerSelectedIndex;

				// Gray out bypassed scalers, unavailable FluidSynth, and an unset Custom preset.
				const bool grayed = (currentPicker == MENU_ITEM_SCALER
				                     && render_supersample != 1 && !scaler_is_plain((uint)i))
				                 || (currentPicker == MENU_ITEM_MUSIC_DEVICE
				                     && (MusicDevice)i == FLUIDSYNTH && !soundfont_available())
				                 || (currentPicker == MENU_ITEM_ENH_PRESET
				                     && (EnhancementPreset)i == ENH_PRESET_CUSTOM
				                     && !enhancementCustomAvailable());

				const char *value = selectedMenuItem->getPickerItem(i, buffer, sizeof buffer);

				draw_font_hv_shadow(VGAScreen, xMenuItemValue, y, value, normal_font, left_aligned, 15,
				                    -3 + (selected ? 2 : 0) + (grayed ? -4 : 0), false, 2);
			}
		}

		if (restart)
		{
			mouseCursor = MOUSE_POINTER_NORMAL;

			fade_palette(shown, 10, 0, 255);

			restart = false;
		}
		else
			set_palette(shown, 0, 255);

		service_SDL_events(true);

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		bool mouseMoved = false;
		int oldFullscreenDisplay = fullscreen_display;
		do
		{
			idle_delay(1);  // fine poll so the cursor redraws at display rate on motion

			float oldMouseX = mouse_xf;
			float oldMouseY = mouse_yf;

			push_joysticks_as_keyboard();
			service_SDL_events(false);

			mouseMoved = mouse_xf != oldMouseX || mouse_yf != oldMouseY;
		} while (!(newkey || newmouse || mouseMoved || fullscreen_display != oldFullscreenDisplay));

		if (currentPicker == MENU_ITEM_NONE)
		{
			// Handle menu item interaction.

			bool action = false;

			if (mouseMoved || newmouse)
			{
				// Find menu item name or value that was hovered or clicked.
				if (mouse_x >= xMenuItem && mouse_x < xMenuItem + wMenuItem)
				{
					for (size_t i = 0; i < menuItemsCount; ++i)
					{
						const int yMenuItem = yMenuItems + dyMenuItems * i;
						if (mouse_y >= yMenuItem && mouse_y < yMenuItem + hMenuItem)
						{
							if (*selectedMenuItemIndex != i)
							{
								JE_playSampleNum(S_CURSOR);
								fpsTyped = false;

								*selectedMenuItemIndex = i;
							}

							if (newmouse && lastmouse_but == SDL_BUTTON_LEFT &&
							    lastmouse_y >= yMenuItem && lastmouse_y < yMenuItem + hMenuItem)
							{
								// Act on menu item via name.
								if (lastmouse_x >= xMenuItemName && lastmouse_x < xMenuItemName + wMenuItemName)
								{
									action = true;
								}

								// Only sliders care where the value column was clicked.
								else if (lastmouse_x >= xMenuItemValue && lastmouse_x < xMenuItemValue + wMenuItemValue)
								{
									switch (menuItems[*selectedMenuItemIndex].id)
									{
									case MENU_ITEM_ENEMY_BAR_OPACITY:
									{
										JE_playSampleNum(S_CURSOR);

										int value = (lastmouse_x - xMenuItemValue) * 100 / (wMenuItemValue - 1);
										enemyBarOpacity = MIN(MAX(0, value), 100);
										break;
									}
									case MENU_ITEM_MUSIC_VOLUME:
									{
										JE_playSampleNum(S_CURSOR);

										int value = (lastmouse_x - xMenuItemValue) * 255 / (wMenuItemValue - 1);
										tyrMusicVolume = MIN(MAX(0, value), 255);

										set_volume(tyrMusicVolume, fxVolume);
										break;
									}
									case MENU_ITEM_SOUND_VOLUME:
									{
										int value = (lastmouse_x - xMenuItemValue) * 255 / (wMenuItemValue - 1);
										fxVolume = MIN(MAX(0, value), 255);

										set_volume(tyrMusicVolume, fxVolume);

										JE_playSampleNum(S_CURSOR);
										break;
									}
									case MENU_ITEM_SHIP_SENS:
									{
										int value = (lastmouse_x - xMenuItemValue) * SHIP_SENS_MAX / (wMenuItemValue - 1);
										ship_sensitivity = MIN(MAX(0, value), SHIP_SENS_MAX);

										JE_playSampleNum(S_CURSOR);
										break;
									}
									case MENU_ITEM_TOUCH_OPACITY:
									{
										int value = (lastmouse_x - xMenuItemValue) * TOUCH_OPACITY_MAX
										            / (wMenuItemValue - 1);
										touchButtonOpacity = MIN(MAX(0, value), TOUCH_OPACITY_MAX);

										JE_playSampleNum(S_CURSOR);
										break;
									}
									default:
										action = true;
										break;
									}
								}
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

					currentMenu = menuParents[currentMenu];
				}
			}
			else if (newkey)
			{
				const bool fpsRow = menuItems[*selectedMenuItemIndex].id == MENU_ITEM_FPS;
				const int digit = scancode_digit(lastkey_scan);

				// Digits type an FPS cap directly (desktop keyboard); arrows still step by 5.
				if (fpsRow && digit >= 0)
				{
					fps_cap = fpsTyped && fps_cap < 100 ? fps_cap * 10 + digit
					        : fpsTyped                  ? fps_cap
					                                    : digit;
					fpsTyped = true;
					set_fps(fps_cap);
					JE_playSampleNum(S_CURSOR);
				}
				else if (fpsRow && lastkey_scan == SDL_SCANCODE_BACKSPACE)
				{
					fps_cap /= 10;
					fpsTyped = true;
					set_fps(fps_cap);
					JE_playSampleNum(S_CURSOR);
				}
				else switch (lastkey_scan)
				{
				case SDL_SCANCODE_UP:
				{
					JE_playSampleNum(S_CURSOR);
					fpsTyped = false;

					*selectedMenuItemIndex = *selectedMenuItemIndex == 0
						? menuItemsCount - 1
						: *selectedMenuItemIndex - 1;
					break;
				}
				case SDL_SCANCODE_DOWN:
				{
					JE_playSampleNum(S_CURSOR);
					fpsTyped = false;

					*selectedMenuItemIndex = *selectedMenuItemIndex == menuItemsCount - 1
						? 0
						: *selectedMenuItemIndex + 1;
					break;
				}
				case SDL_SCANCODE_LEFT:
				{
					fpsTyped = false;
					adjustMenuItemValue(&menuItems[*selectedMenuItemIndex], -1);
					JE_applyItemDataSettings();  // land the change now, not at the next episode load
					break;
				}
				case SDL_SCANCODE_RIGHT:
				{
					fpsTyped = false;
					adjustMenuItemValue(&menuItems[*selectedMenuItemIndex], +1);
					JE_applyItemDataSettings();
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
					fpsTyped = false;

					currentMenu = menuParents[currentMenu];
					break;
				}
				default:
					break;
				}
			}

			if (action)
			{
				const MenuItem *const selectedMenuItem = &menuItems[*selectedMenuItemIndex];
				const MenuItemId selectedMenuItemId = selectedMenuItem->id;

				/* Handle common submenu, toggle, and picker actions before row-specific cases. */
				bool *const boolSetting = menuItemBoolSetting(selectedMenuItemId);
				const int *const intSetting = menuItemIntSetting(selectedMenuItemId);

				if (selectedMenuItem->submenu != MENU_NONE)
				{
					JE_playSampleNum(S_SELECT);

					menuParents[selectedMenuItem->submenu] = currentMenu;
					currentMenu = selectedMenuItem->submenu;
					selectedMenuItemIndexes[currentMenu] = 0;
				}
				else if (boolSetting != NULL)
				{
					if (selectedMenuItemId == MENU_ITEM_SUBPIXEL_FX && !subpixelFxAvailable())
						JE_playSampleNum(S_SPRING);
					else
					{
						*boolSetting = !*boolSetting;
						JE_applyItemDataSettings();  // covers the Charge-Laser row
						JE_playSampleNum(S_CLICK);
					}
				}
				else if (intSetting != NULL && menuItemHasPicker(selectedMenuItem))
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = (size_t)*intSetting;
				}
				else switch (selectedMenuItemId)
				{
				case MENU_ITEM_ENH_PRESET:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = (size_t)enhancementPresetState();
					break;
				}
				case MENU_ITEM_DONE:
				{
					JE_playSampleNum(S_SELECT);

					currentMenu = menuParents[currentMenu];
					break;
				}
				case MENU_ITEM_INSTRUCTIONS:
				{
					JE_playSampleNum(S_SELECT);

					fade_black(10);

					// The help system plays its own track; put the menu's back afterwards.
					const unsigned int song = song_playing;
					JE_helpSystem(1);
					play_song(song);

					restart = true;
					break;
				}
				case MENU_ITEM_JUKEBOX:
				{
					JE_playSampleNum(S_SELECT);

					fade_black(10);
					set_menu_centered(false);

					jukebox();

					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_DESTRUCT:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);

					set_menu_centered(false);
					JE_destructGame();
					set_menu_centered(true);

					restart = true;
					break;
				}
				case MENU_ITEM_SUPERTYRIAN:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);

					if (newSuperTyrianGame())
						return true;  // game launched; the title screen starts it

					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_WEAPON_EDITOR:
				{
					JE_playSampleNum(S_SELECT);
					JE_weaponEditor(false);  // Setup context: design only (no active ship to equip)
					restart = true;
					break;
				}
				case MENU_ITEM_SHIP_EDITOR:
				{
					JE_playSampleNum(S_SELECT);
					JE_shipEditor(false);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_SAVE_UPLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					JE_saveTransferUpload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_SAVE_DOWNLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					JE_saveTransferDownload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_SAVES_UPLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					savesXferUpload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_SAVES_DOWNLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					savesXferDownload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_SHIPS_UPLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					shipsXferUpload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_SHIPS_DOWNLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					shipsXferDownload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_WEAPONS_UPLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					weaponsXferUpload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_WEAPONS_DOWNLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					weaponsXferDownload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_LEVELS_UPLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					levelsXferUpload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_LEVELS_DOWNLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					levelsXferDownload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_CUSTOM_UPLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					customXferUpload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_CUSTOM_DOWNLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					customXferDownload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_ALL_UPLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					allXferUpload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_TRANSFER_ALL_DOWNLOAD:
				{
					JE_playSampleNum(S_SELECT);
					fade_black(10);
					allXferDownload();
					set_menu_centered(true);
					restart = true;
					break;
				}
				case MENU_ITEM_DISPLAY:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = (size_t)(fullscreen_display + 1);
					break;
				}
				case MENU_ITEM_SCALER:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = scaler;
					break;
				}
				case MENU_ITEM_SCALING_MODE:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = scaling_mode;
					break;
				}
				case MENU_ITEM_SUPERSAMPLE:
				{
					if (!smoothMotion)
					{
						JE_playSampleNum(S_SPRING);
						break;
					}

					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = (size_t)render_supersample;
					break;
				}
				case MENU_ITEM_RENDER_THREADS:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = renderThreadChoiceIndex();
					break;
				}
				case MENU_ITEM_MUSIC_DEVICE:
				{
					JE_playSampleNum(S_CLICK);

					currentPicker = selectedMenuItemId;
					pickerSelectedIndex = music_device;
					break;
				}
				case MENU_ITEM_FPS:
				{
#ifdef PLATFORM_HANDHELD
					// No physical keyboard here; the system keypad stands in
					// for the desktop's typed digits.
					char kb[8];
					snprintf(kb, sizeof(kb), "%d", fps_cap);
					if (console_swkbd(kb, sizeof(kb), 3, kb, "FPS cap (0 = uncapped)", true))
					{
						// Filter, not trust: the Vita IME has no numeric mode.
						int v = 0;
						for (const char *c = kb; *c != '\0'; ++c)
							if (*c >= '0' && *c <= '9' && v < 100)
								v = v * 10 + (*c - '0');
						fps_cap = v;
						set_fps(fps_cap);
					}
					// Drop the press that opened the keypad and anything it left behind,
					// so this menu does not act on it a second time.
					wait_noinput(true, true, true);
					service_SDL_events(true);
					newkey = newmouse = false;
#else
					fpsTyped = false;  // commit; the next digit starts a fresh number
#endif
					JE_playSampleNum(S_CLICK);
					break;
				}
				case MENU_ITEM_MUSIC_VOLUME:
				{
					JE_playSampleNum(S_CLICK);

					set_music_disabled(!music_disabled);
					if (!music_disabled)
						restart_song();
					break;
				}
				case MENU_ITEM_SOUND_VOLUME:
				{
					samples_disabled = !samples_disabled;

					JE_playSampleNum(S_CLICK);
					break;
				}
				case MENU_ITEM_VSYNC:
				{
					set_vsync(!output_vsync);
					JE_playSampleNum(S_CLICK);
					break;
				}
				case MENU_ITEM_SMOOTH_MOTION:
				{
					set_smooth_motion(!smoothMotion);
					enforcePlainScalerForSupersample();  // turning on re-arms Auto; scaler rule applies
					JE_playSampleNum(S_CLICK);
					break;
				}
				case MENU_ITEM_NET_LOG:
				{
					crashlog_set_netlog_enabled(!crashlog_get_netlog_enabled());
					JE_playSampleNum(S_CLICK);
					break;
				}
				case MENU_ITEM_CLEAR_LOGS:
				{
					logsCleared = crashlog_clear_logs() ? LOGS_CLEAR_DONE : LOGS_CLEAR_ABSENT;
					JE_playSampleNum(logsCleared == LOGS_CLEAR_DONE ? S_SELECT : S_CLICK);
					break;
				}
				case MENU_ITEM_CLEAR_CLV:
				{
					// The visibility filter hides this row after a successful clear.
					customEpisodeClearAll();
					JE_playSampleNum(S_SELECT);
					break;
				}
				case MENU_ITEM_XMAS:
				{
					JE_playSampleNum(toggle_xmas_mode() ? S_CLICK : S_SPRING);
					break;
				}
				case MENU_ITEM_UNUSED_SPRITES:
				{
					unusedShopSprites = !unusedShopSprites;
					JE_applyUnusedShopSprites();  // repaint the item table now, not at the next episode load
					JE_playSampleNum(S_CLICK);
					break;
				}
				case MENU_ITEM_SHOT_HITBOXES:
				{
					centeredShotHitboxes = !centeredShotHitboxes;
					JE_playSampleNum(S_CLICK);
					break;
				}
				case MENU_ITEM_SIDEKICK_AUTOFIRE:
				{
					cycleSidekickAutofire(+1);
					JE_playSampleNum(S_CLICK);
					break;
				}
				default:
					// Super Arcade ship rows: launch that ship's game.
					if (selectedMenuItemId >= MENU_ITEM_ARCADE_SHIP_BASE)
					{
						JE_playSampleNum(S_SELECT);
						fade_black(10);

						if (newSuperArcadeGame(selectedMenuItemId - MENU_ITEM_ARCADE_SHIP_BASE))
							return true;  // game launched; the title screen starts it

						set_menu_centered(true);
						restart = true;
					}
					break;
				}
			}

			if (currentMenu == MENU_NONE)
			{
				// Save menu changes immediately because console HOME exits may skip normal shutdown.
				save_opentyrian_config();

				fade_black(10);

				return false;
			}
		}
		else
		{
			const MenuItem *selectedMenuItem = &menuItems[*selectedMenuItemIndex];

			// Handle picker interaction.

			bool action = false;

			if (mouseMoved || newmouse)
			{
				const size_t pickerItemsCount = selectedMenuItem->getPickerItemsCount();

				// Find picker item that was hovered clicked.
				if (mouse_x >= xMenuItemValue && mouse_x < xMenuItemValue + wMenuItemValue)
				{
					for (size_t i = 0; i < pickerItemsCount; ++i)
					{
						const int yPickerItem = yPicker + dyPickerItem * i;

						if (mouse_y >= yPickerItem && mouse_y < yPickerItem + hPickerItem)
						{
							if (pickerSelectedIndex != i)
							{
								JE_playSampleNum(S_CURSOR);

								pickerSelectedIndex = i;
							}

							// Act on picker item.
							if (newmouse && lastmouse_but == SDL_BUTTON_LEFT &&
							    lastmouse_x >= xMenuItemValue && lastmouse_y < xMenuItemValue + wMenuItemName &&
							    lastmouse_y >= yPickerItem && lastmouse_y < yPickerItem + hPickerItem)
							{
								action = true;
							}
						}
					}
				}
			}

			if (newmouse)
			{
				if (lastmouse_but == SDL_BUTTON_RIGHT)
				{
					JE_playSampleNum(S_SPRING);

					currentPicker = MENU_ITEM_NONE;
				}
			}
			else if (newkey)
			{
				switch (lastkey_scan)
				{
				case SDL_SCANCODE_UP:
				{
					JE_playSampleNum(S_CURSOR);

					const size_t pickerItemsCount = selectedMenuItem->getPickerItemsCount();

					pickerSelectedIndex = pickerSelectedIndex == 0
						? pickerItemsCount - 1
						: pickerSelectedIndex - 1;
					break;
				}
				case SDL_SCANCODE_DOWN:
				{
					JE_playSampleNum(S_CURSOR);

					const size_t pickerItemsCount = selectedMenuItem->getPickerItemsCount();

					pickerSelectedIndex = pickerSelectedIndex == pickerItemsCount - 1
						? 0
						: pickerSelectedIndex + 1;
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

					currentPicker = MENU_ITEM_NONE;
					break;
				}
				default:
					break;
				}
			}

			if (action)
			{
				// The index-backed settings commit straight into the setting the row maps to;
				// the rest need work beyond storing the index.
				int *const intSetting = menuItemIntSetting(selectedMenuItem->id);
				if (intSetting != NULL)
				{
					*intSetting = (int)pickerSelectedIndex;
					JE_applyItemDataSettings();  // land the change now, not at the next episode load
				}

				// After the write, so a Firing Sounds row answers with the sound it just chose.
				playMenuItemSample(selectedMenuItem->id, S_CLICK);

				if (intSetting == NULL) switch (selectedMenuItem->id)
				{
				case MENU_ITEM_ENH_PRESET:
				{
					// Custom is grayed out until there is a set of the player's own to restore.
					if ((EnhancementPreset)pickerSelectedIndex == ENH_PRESET_CUSTOM
					    && !enhancementCustomAvailable())
					{
						JE_playSampleNum(S_SPRING);
						break;
					}
					enhancementApplyPreset((EnhancementPreset)pickerSelectedIndex);
					break;
				}
				case MENU_ITEM_DISPLAY:
				{
					if ((int)pickerSelectedIndex - 1 != fullscreen_display)
						reinit_fullscreen((int)pickerSelectedIndex - 1);
					break;
				}
				case MENU_ITEM_SCALER:
				{
					// Algorithm scalers are grayed out while Sub-pixel is on; refuse.
					if (render_supersample != 1 && !scaler_is_plain(pickerSelectedIndex))
					{
						JE_playSampleNum(S_SPRING);
						break;
					}
					if (pickerSelectedIndex != scaler)
					{
						const int oldScaler = scaler;
						if (!init_scaler(pickerSelectedIndex) &&  // try new scaler
							!init_scaler(oldScaler))              // revert on fail
						{
							JE_tyrianExitWithoutSaving(EXIT_FAILURE);
						}
					}
					break;
				}
				case MENU_ITEM_SCALING_MODE:
				{
					scaling_mode = pickerSelectedIndex;
					break;
				}
				case MENU_ITEM_SUPERSAMPLE:
				{
					render_supersample = smoothMotion ? (int)pickerSelectedIndex : 1;
					enforcePlainScalerForSupersample();
					break;
				}
				case MENU_ITEM_RENDER_THREADS:
				{
					int choices[RENDER_THREAD_CHOICES_MAX];
					renderThreadChoices(choices);
					render_threads = choices[pickerSelectedIndex];
					break;
				}
				case MENU_ITEM_MUSIC_DEVICE:
				{
					// FluidSynth is grayed out with no SoundFont to load; refuse.
					if ((MusicDevice)pickerSelectedIndex == FLUIDSYNTH && !soundfont_available())
					{
						JE_playSampleNum(S_SPRING);
						break;
					}
					music_device = (MusicDevice)pickerSelectedIndex;
					restart_audio();
					break;
				}
				default:
					break;
				}

				currentPicker = MENU_ITEM_NONE;
			}
		}
	}
}

bool setupMenuSelfTest(char *detail, size_t detailSize)
{
	const char *fault = NULL;
	for (int i = 0; i < REWORK_COUNT && fault == NULL; ++i)
		if (menuItemBoolSetting((MenuItemId)(MENU_ITEM_REWORK_BASE + i)) != &reworkEnabled[i])
			fault = "a Reworks row does not toggle its own rework";
	if (fault == NULL && menuItemIntSetting(MENU_ITEM_ZICA_BASE) != &zicaLaserBase)
		fault = "the Zica Laser Lv11 row does not pick the Lv11 pattern";
	if (fault == NULL && menuItemBoolSetting(MENU_ITEM_ARCADE_SHIP_BASE) != NULL)
		fault = "the arcade ship rows overlap the Reworks rows";

	if (fault != NULL && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault == NULL;
}
