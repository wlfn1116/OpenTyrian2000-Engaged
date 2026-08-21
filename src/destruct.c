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

/* Destruct is a two-player artillery mode with legacy global player state.
 * DESTRUCT_MODES counts data-backed modes; MAX_MODES also includes Custom. */

#include "destruct.h"
#include "destruct_rollback.h"

#include "config.h"
#include "crashlog.h"
#include "config_file.h"
#include "file.h"
#include "fonthand.h"
#include "helptext.h"
#include "joystick.h"
#include "keyboard.h"
#include "loudness.h"
#include "mtrand.h"
#include "net_rollback.h"
#include "network.h"
#include "nortsong.h"
#include "opentyr.h"
#include "palette.h"
#include "picload.h"
#include "qa.h"
#include "sim_math.h"
#include "sprite.h"
#include "touch_ui.h"
#include "varz.h"
#include "vga256d.h"
#include "video.h"

#include <assert.h>

#define MAX_KEY_OPTIONS 4

/* Widescreen Destruct HUD layout.  Each player's readout is a HUD_FRAME_W-wide
 * frame lifted from pic #11 with the drawn box sitting 1px inside it, so the
 * frame's left edge is HUD_BOX_OFFSET px right of DrawHUD's startX anchor.  The
 * two frames are pinned flush against the screen edges, symmetric about center. */
#define HUD_FRAME_W        144
#define HUD_FRAME_LEFT_X   0
#define HUD_FRAME_RIGHT_X  (vga_width - HUD_FRAME_W)
#define HUD_BOX_OFFSET     2
#define HUD_ROWS           12   /* rows 0..HUD_ROWS-1 are the HUD strip; the playfield is below */
#define HUD_GAP_LEFT       (HUD_FRAME_LEFT_X + HUD_FRAME_W)  /* first column of the playfield gap between the boxes */

enum de_state_t
{
	STATE_INIT,
	STATE_RELOAD,
	STATE_CONTINUE
};

enum de_player_t
{
	PLAYER_LEFT = 0,
	PLAYER_RIGHT = 1,
	MAX_PLAYERS = 2
};

enum de_team_t
{
	TEAM_LEFT = 0,
	TEAM_RIGHT = 1,
	MAX_TEAMS = 2
};

enum de_mode_t
{
	MODE_5CARDWAR = 0,
	MODE_TRADITIONAL,
	MODE_HELIASSAULT,
	MODE_HELIDEFENSE,
	MODE_OUTGUNNED,
	MODE_CUSTOM,
	MODE_FIRST = MODE_5CARDWAR,
	MODE_LAST = MODE_CUSTOM,
	MAX_MODES = 6,
	MODE_NONE = -1
};

enum de_unit_t
{
	UNIT_TANK = 0,
	UNIT_NUKE,
	UNIT_DIRT,
	UNIT_SATELLITE,
	UNIT_MAGNET,
	UNIT_LASER,
	UNIT_JUMPER,
	UNIT_HELI,
	UNIT_FIRST = UNIT_TANK,
	UNIT_LAST = UNIT_HELI,
	MAX_UNITS = 8,
	UNIT_NONE = -1
};

enum de_shot_t
{
	SHOT_TRACER = 0,
	SHOT_SMALL,
	SHOT_LARGE,
	SHOT_MICRO,
	SHOT_SUPER,
	SHOT_DEMO,
	SHOT_SMALLNUKE,
	SHOT_LARGENUKE,
	SHOT_SMALLDIRT,
	SHOT_LARGEDIRT,
	SHOT_MAGNET,
	SHOT_MINILASER,
	SHOT_MEGALASER,
	SHOT_LASERTRACER,
	SHOT_MEGABLAST,
	SHOT_MINI,
	SHOT_BOMB,
	SHOT_FIRST = SHOT_TRACER,
	SHOT_LAST = SHOT_BOMB,
	MAX_SHOT_TYPES = 17,
	SHOT_INVALID = -1
};

enum de_expl_t
{
	EXPL_NONE,
	EXPL_MAGNET,
	EXPL_DIRT,
	EXPL_NORMAL
}; /* this needs a better name */

enum de_trails_t
{
	TRAILS_NONE,
	TRAILS_NORMAL,
	TRAILS_FULL
};

enum de_pixel_t
{
	PIXEL_BLACK = 0,
	PIXEL_DIRT = 25
};

enum de_mapflags_t
{
	MAP_NORMAL = 0x00,
	MAP_WALLS = 0x01,
	MAP_RINGS = 0x02,
	MAP_HOLES = 0x04,
	MAP_FUZZY = 0x08,
	MAP_TALL = 0x10
};

/* keys and moves should line up. */
enum de_keys_t
{
	KEY_LEFT = 0,
	KEY_RIGHT,
	KEY_UP,
	KEY_DOWN,
	KEY_CHANGE,
	KEY_FIRE,
	KEY_CYUP,
	KEY_CYDN,
	MAX_KEY = 8
};

enum de_move_t
{
	MOVE_LEFT = 0,
	MOVE_RIGHT,
	MOVE_UP,
	MOVE_DOWN,
	MOVE_CHANGE,
	MOVE_FIRE,
	MOVE_CYUP,
	MOVE_CYDN,
	MAX_MOVE = 8
};

/* Tracer laser remains unassigned, and the bomb remains nonfunctional. */

struct destruct_config_s
{
	unsigned int max_shots;
	unsigned int min_walls;
	unsigned int max_walls;
	unsigned int max_explosions;
	unsigned int max_installations;
	bool allow_custom;
	bool alwaysalias;
	bool jumper_straight[2];
	bool ai[2];
};

struct destruct_unit_s
{
	/* Positioning/movement */
	unsigned int unitX; /* yep, one's an int and the other is a real */
	float        unitY;
	float        unitYMov;
	bool         isYInAir;

	/* Position at the start of the current tick, for the smooth present's
	 * interpolation (see DE_SmoothPresent). */
	float        prev_x, prev_y;

	/* What it is and what it fires */
	enum de_unit_t unitType;
	enum de_shot_t shotType;

	/* What it's pointed */
	float angle;
	float power;

	/* Misc */
	int lastMove;
	unsigned int ani_frame;
	int health;
};

struct destruct_shot_s
{
	bool isAvailable;

	float x;
	float y;
	float prev_x, prev_y;  /* position at the start of the tick, for smooth interpolation */
	float xmov;
	float ymov;
	bool gravity;
	unsigned int shottype;
	//int shotdur; /* This looks to be unused */
	unsigned int trailx[4], traily[4], trailc[4];
};

struct destruct_explo_s
{
	bool isAvailable;

	unsigned int x, y;
	unsigned int explowidth;
	unsigned int explomax;
	unsigned int explofill;
	enum de_expl_t exploType;
};

struct destruct_moves_s
{
	bool actions[MAX_MOVE];
};

struct destruct_keys_s
{
	SDL_Scancode Config[MAX_KEY][MAX_KEY_OPTIONS];
};

struct destruct_ai_s
{
	int c_Angle, c_Power, c_Fire;
	unsigned int c_noDown;
};

struct destruct_player_s
{
	bool is_cpu;
	struct destruct_ai_s aiMemory;

	struct destruct_unit_s* unit;
	struct destruct_moves_s moves;
	struct destruct_keys_s  keys;

	enum de_team_t team;
	unsigned int unitsRemaining;
	unsigned int unitSelected;
	unsigned int shotDelay;
	unsigned int score;
};

struct destruct_wall_s
{
	bool wallExist;
	unsigned int wallX, wallY;
};

struct destruct_world_s
{
	/* Map data & screen pointer */
	unsigned int baseMap[vga_width];
	SDL_Surface* VGAScreen;
	struct destruct_wall_s* mapWalls;

	/* Map configuration */
	enum de_mode_t destructMode;
	unsigned int mapFlags;
};

//Prep functions
static void JE_destructMain(void);
static void JE_introScreen(void);
static enum de_mode_t JE_modeSelect(void);
static void JE_helpScreen(void);
static void JE_pauseScreen(void);

//level generating functions
static void JE_generateTerrain(void);
static void DE_generateBaseTerrain(unsigned int, unsigned int*);
static void DE_drawBaseTerrain(unsigned int*);
static void DE_generateUnits(unsigned int*);
static void DE_generateWalls(struct destruct_world_s*);
static void DE_generateRings(SDL_Surface*, Uint8);
static void DE_ResetLevel(void);
static unsigned int JE_placementPosition(unsigned int, unsigned int, unsigned int*);

//drawing functions
static void JE_aliasDirt(SDL_Surface*);
static void DE_RunTickDrawCrosshairs(void);
static void DE_widenHUDBackdrop(SDL_Surface*);
static void DE_RunTickDrawHUD(void);
static void DE_GravityDrawUnit(enum de_player_t, struct destruct_unit_s*);
static unsigned int DE_unitAnimIndex(enum de_player_t, const struct destruct_unit_s*);
static void DE_RunTickAnimate(void);
static void DE_RunTickDrawWalls(void);
static void DE_DrawTrails(struct destruct_shot_s*, unsigned int, unsigned int, unsigned int);
static void DE_blendTempPixel(int, int);
static void JE_tempScreenChecking(void);
static void JE_superPixel(unsigned int, unsigned int);
static void JE_pixCool(unsigned int, unsigned int, Uint8);

//player functions
static void DE_RunTickGetInput(void);
static void DE_ProcessInput(void);
static void DE_ResetPlayers(void);
static void DE_ResetAI(void);
static void DE_ResetActions(void);
static void DE_RunTickAI(void);

//unit functions
static void DE_RaiseAngle(struct destruct_unit_s*);
static void DE_LowerAngle(struct destruct_unit_s*);
static void DE_RaisePower(struct destruct_unit_s*);
static void DE_LowerPower(struct destruct_unit_s*);
static void DE_CycleWeaponUp(struct destruct_unit_s*);
static void DE_CycleWeaponDown(struct destruct_unit_s*);
static void DE_RunMagnet(enum de_player_t, struct destruct_unit_s*);
static void DE_GravityFlyUnit(struct destruct_unit_s*);
static void DE_GravityLowerUnit(struct destruct_unit_s*);
static void DE_DestroyUnit(enum de_player_t, struct destruct_unit_s*);
static void DE_ResetUnits(void);
static inline bool DE_isValidUnit(struct destruct_unit_s*);

//weapon functions
static void DE_ResetWeapons(void);
static void DE_RunTickShots(void);
static void DE_RunTickExplosions(void);
static void DE_TestExplosionCollision(unsigned int, unsigned int);
static void JE_makeExplosion(unsigned int, unsigned int, enum de_shot_t);
static void DE_MakeShot(enum de_player_t, const struct destruct_unit_s*, int);

//smooth (interpolated + supersampled) present
static void DE_pixScaled(SDL_Surface*, int, int, Uint8, int);
static void DE_pixCoolScaled(SDL_Surface*, int, int, Uint8, int);
static void DE_ExpandBackgroundHi(int);
static void DE_ExpandHUD(SDL_Surface*, int);
static void DE_DrawWallsScaled(SDL_Surface*, int);
static void DE_DrawUnitsScaled(SDL_Surface*, int, float);
static void DE_DrawShotsScaled(SDL_Surface*, int, float);
static void DE_DrawCrosshairsScaled(SDL_Surface*, int, float);
static void DE_ComposeFrame(SDL_Surface*, int, float);
static void DE_SmoothPresent(int);

//gameplay functions
static enum de_state_t DE_RunTick(void);
static void DE_RunTickCycleDeadUnits(void);
static void DE_RunTickGravity(void);
static bool DE_RunTickCheckEndgame(void);
static bool JE_stabilityCheck(unsigned int, unsigned int);

// Sound
static void DE_RunTickPlaySounds(void);
static void JE_eSound(unsigned int);

#ifdef WITH_NETWORK
// online netcode
static Uint32 DE_NetSimHash(void);
static size_t DE_StateSize(void);
static void DE_StateSave(void* dst);
static void DE_StateRestore(const void* src);
#endif

// Utility functions
static int center_text(const char* s, unsigned int font)
{
	return (vga_width - JE_textWidth(s, font)) / 2;
}

static const bool     demolish[MAX_SHOT_TYPES] = { false, false, false, false, false, true, true, true, false, false, false, false, true, false, true, false, true };
static const int     shotTrail[MAX_SHOT_TYPES] = { TRAILS_NONE, TRAILS_NONE, TRAILS_NONE, TRAILS_NORMAL, TRAILS_NORMAL, TRAILS_NORMAL, TRAILS_FULL, TRAILS_FULL, TRAILS_NONE, TRAILS_NONE, TRAILS_NONE, TRAILS_NORMAL, TRAILS_FULL, TRAILS_NORMAL, TRAILS_FULL, TRAILS_NORMAL, TRAILS_NONE };
static const int     shotDelay[MAX_SHOT_TYPES] = { 10, 30, 80, 20, 60, 100, 140, 200, 20, 60, 5, 15, 50, 5, 80, 16, 0 };
static const int     shotSound[MAX_SHOT_TYPES] = { S_SELECT, S_WEAPON_2, S_WEAPON_1, S_WEAPON_7, S_WEAPON_7, S_EXPLOSION_9, S_EXPLOSION_22, S_EXPLOSION_22, S_WEAPON_5, S_WEAPON_13, S_WEAPON_10, S_WEAPON_15, S_WEAPON_15, S_WEAPON_26, S_WEAPON_14, S_WEAPON_7, S_WEAPON_7 };
static const int     exploSize[MAX_SHOT_TYPES] = { 4, 20, 30, 14, 22, 16, 40, 60, 10, 30, 0, 5, 10, 3, 15, 7, 0 };
static const bool   shotBounce[MAX_SHOT_TYPES] = { false, false, false, false, false, false, false, false, false, false, false, true, true, true, true, false, true };
static const int  exploDensity[MAX_SHOT_TYPES] = { 2,  5, 10, 15, 20, 15, 25, 30, 40, 80, 0, 30, 30,  4, 30, 5, 0 };
static const int      shotDirt[MAX_SHOT_TYPES] = { EXPL_NORMAL, EXPL_NORMAL, EXPL_NORMAL, EXPL_NORMAL, EXPL_NORMAL, EXPL_NORMAL, EXPL_NORMAL, EXPL_NORMAL, EXPL_DIRT, EXPL_DIRT, EXPL_MAGNET, EXPL_NORMAL, EXPL_NORMAL, EXPL_NORMAL, EXPL_NORMAL, EXPL_NORMAL, EXPL_NONE };
static const int     shotColor[MAX_SHOT_TYPES] = { 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 10, 10, 10, 10, 16, 0 };

static const int     defaultWeapon[MAX_UNITS] = { SHOT_SMALL, SHOT_MICRO,     SHOT_SMALLDIRT, SHOT_INVALID, SHOT_MAGNET, SHOT_MINILASER, SHOT_MICRO, SHOT_MINI };
static const int  defaultCpuWeapon[MAX_UNITS] = { SHOT_SMALL, SHOT_MICRO,     SHOT_DEMO,      SHOT_INVALID, SHOT_MAGNET, SHOT_MINILASER, SHOT_MICRO, SHOT_MINI };
static const int defaultCpuWeaponB[MAX_UNITS] = { SHOT_DEMO,  SHOT_SMALLNUKE, SHOT_DEMO,      SHOT_INVALID, SHOT_MAGNET, SHOT_MEGALASER, SHOT_MICRO, SHOT_MINI };
static const int       systemAngle[MAX_UNITS] = { true, true, true, false, false, true, false, false };
static const int        baseDamage[MAX_UNITS] = { 200, 120, 400, 300, 80, 150, 600, 40 };
static const int         systemAni[MAX_UNITS] = { false, false, false, true, false, false, false, true };

static bool weaponSystems[MAX_UNITS][MAX_SHOT_TYPES] =
{
	{1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // normal
	{0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // nuke
	{0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0}, // dirt
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // worthless
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0}, // magnet
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0}, // laser
	{1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0}, // jumper
	{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0}  // helicopter
};

/* More constant configuration settings. */
/* Music that destruct will play.  You can check out musmast.c to see what is what. */
static const JE_byte goodsel[14] /*[1..14]*/ = { 1, 2, 6, 12, 13, 14, 17, 23, 24, 26, 28, 29, 32, 33 };

/* Unit creation.  Need to move this later: Doesn't belong here */
static JE_byte basetypes[10][11] /*[1..8, 1..11]*/ = /* [0] is amount of units*/
{
	{5, UNIT_TANK, UNIT_TANK, UNIT_NUKE, UNIT_DIRT,      UNIT_DIRT,   UNIT_SATELLITE, UNIT_MAGNET, UNIT_LASER,  UNIT_JUMPER, UNIT_HELI},   /*Normal*/
	{1, UNIT_TANK, UNIT_TANK, UNIT_TANK, UNIT_TANK,      UNIT_TANK,   UNIT_TANK,      UNIT_TANK,   UNIT_TANK,   UNIT_TANK,   UNIT_TANK},   /*Traditional*/
	{4, UNIT_HELI, UNIT_HELI, UNIT_HELI, UNIT_HELI,      UNIT_HELI,   UNIT_HELI,      UNIT_HELI,   UNIT_HELI,   UNIT_HELI,   UNIT_HELI},   /*Weak   Heli attack fleet*/
	{8, UNIT_TANK, UNIT_TANK, UNIT_TANK, UNIT_NUKE,      UNIT_NUKE,   UNIT_NUKE,      UNIT_DIRT,   UNIT_MAGNET, UNIT_LASER,  UNIT_JUMPER}, /*Strong Heli defense fleet*/
	{8, UNIT_HELI, UNIT_HELI, UNIT_HELI, UNIT_HELI,      UNIT_HELI,   UNIT_HELI,      UNIT_HELI,   UNIT_HELI,   UNIT_HELI,   UNIT_HELI},   /*Strong Heli attack fleet*/
	{4, UNIT_TANK, UNIT_TANK, UNIT_TANK, UNIT_TANK,      UNIT_NUKE,   UNIT_NUKE,      UNIT_DIRT,   UNIT_MAGNET, UNIT_JUMPER, UNIT_JUMPER}, /*Weak   Heli defense fleet*/
	{8, UNIT_TANK, UNIT_NUKE, UNIT_DIRT, UNIT_SATELLITE, UNIT_MAGNET, UNIT_LASER,     UNIT_JUMPER, UNIT_HELI,   UNIT_TANK,   UNIT_NUKE},   /*Overpowering fleet*/
	{4, UNIT_TANK, UNIT_TANK, UNIT_NUKE, UNIT_DIRT,      UNIT_TANK,   UNIT_LASER,     UNIT_JUMPER, UNIT_HELI,   UNIT_NUKE,   UNIT_JUMPER}, /*Weak fleet*/
	{5, UNIT_TANK, UNIT_TANK, UNIT_NUKE, UNIT_DIRT,      UNIT_DIRT,   UNIT_SATELLITE, UNIT_MAGNET, UNIT_LASER,  UNIT_JUMPER, UNIT_HELI},   /*Left custom*/
	{5, UNIT_TANK, UNIT_TANK, UNIT_NUKE, UNIT_DIRT,      UNIT_DIRT,   UNIT_SATELLITE, UNIT_MAGNET, UNIT_LASER,  UNIT_JUMPER, UNIT_HELI},   /*Right custom*/
};
static const unsigned int baseLookup[MAX_PLAYERS][MAX_MODES] =
{
	{0, 1, 3, 4, 6, 8},
	{0, 1, 2, 5, 7, 9}
};

static const JE_byte GraphicBase[MAX_PLAYERS][MAX_UNITS] =
{
	{  1,   6,  11,  58,  63,  68,  96, 153},
	{ 20,  25,  30,  77,  82,  87, 115, 172}
};

static const JE_byte ModeScore[MAX_PLAYERS][MAX_MODES] =
{
	{1, 0, 0, 5, 0, 1},
	{1, 0, 5, 0, 1, 1}
};

static SDL_Scancode defaultKeyConfig[MAX_PLAYERS][MAX_KEY][MAX_KEY_OPTIONS] =
{
	{
		{SDL_SCANCODE_C},
		{SDL_SCANCODE_V},
		{SDL_SCANCODE_A},
		{SDL_SCANCODE_Z},
		{SDL_SCANCODE_LALT},
		{SDL_SCANCODE_X, SDL_SCANCODE_LSHIFT},
		{SDL_SCANCODE_LCTRL},
		{SDL_SCANCODE_SPACE}
	},
	{
		{SDL_SCANCODE_LEFT, SDL_SCANCODE_KP_4},
		{SDL_SCANCODE_RIGHT, SDL_SCANCODE_KP_6},
		{SDL_SCANCODE_UP, SDL_SCANCODE_KP_8},
		{SDL_SCANCODE_DOWN, SDL_SCANCODE_KP_2},
		{SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_KP_5},
		{SDL_SCANCODE_INSERT, SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_ENTER},
		{SDL_SCANCODE_PAGEUP, SDL_SCANCODE_KP_9},
		{SDL_SCANCODE_PAGEDOWN, SDL_SCANCODE_KP_3}
	}
};

static SDL_Surface* destructTempScreen;
static JE_boolean destructFirstTime;

/* Ticks left before the finished round reloads.  Simulation state rather than a tick-local
 * counter, because a rollback re-simulation has to restore and re-derive it. */
static unsigned int de_endDelay;

/* Clean copy of the HUD strip (rows 0..HUD_ROWS-1), captured by
 * DE_widenHUDBackdrop and repainted every tick by DE_RunTickDrawHUD so gameplay
 * pixels can't accumulate on the HUD art. */
static Uint8 hudBackdrop[HUD_ROWS * vga_width];

static struct destruct_config_s config = { 40, 20, 20, 40, 10, false, false, {true, false}, {true, false} };
static struct destruct_player_s destruct_player[MAX_PLAYERS];
static struct destruct_world_s  world;
static struct destruct_shot_s* shotRec;
static struct destruct_explo_s* exploRec;

/* Smooth (interpolated + supersampled) present: when Smooth Motion + supersampling are on,
 * present several interpolated NxN frames per tick instead of one. */
static SDL_Surface* destruct_hi = NULL;     /* NxN compose + present buffer */
static SDL_Surface* destruct_bg_hi = NULL;  /* NxN static terrain, rebuilt once per tick */

static bool   destruct_sim_timing_init = false;
static Uint64 destruct_sim_freq = 0, destruct_sim_last = 0;
static float  destruct_sim_accum = 0.0f;

void destruct_deinit(void)
{
	drb_session_end();
	free_sprite2s(&destructSpriteSheet);
	free(shotRec);
	shotRec = NULL;
	free(exploRec);
	exploRec = NULL;
	free(world.mapWalls);
	world.mapWalls = NULL;
	free(destruct_player[PLAYER_LEFT].unit);
	destruct_player[PLAYER_LEFT].unit = NULL;
	free(destruct_player[PLAYER_RIGHT].unit);
	destruct_player[PLAYER_RIGHT].unit = NULL;
	if (destruct_hi != NULL)
	{
		SDL_FreeSurface(destruct_hi);
		destruct_hi = NULL;
	}
	if (destruct_bg_hi != NULL)
	{
		SDL_FreeSurface(destruct_bg_hi);
		destruct_bg_hi = NULL;
	}
}

#ifdef WITH_NETWORK
/* Delay-based Destruct applies both peers' inputs after network_delay ticks. Rollback applies local
 * input immediately and replays from DE_StateSave when the prediction differs. */
static bool de_net = false;                  /* this Destruct run is an online session */
static bool de_net_rollback = false;         /* ...and it runs rollback, not the lockstep below */
static enum de_player_t de_net_local_side;   /* the side this machine's controls drive */
static unsigned int de_net_round;            /* rounds started; salts the shared terrain seed */
static bool de_net_desync_noted;
static bool de_net_have_inputs;              /* false inside the initial delay window */
static Uint8 de_net_local_bits, de_net_peer_bits;

/* Both netcodes carry the same control bits (state packet byte 5 for the lockstep, the input
 * record's second byte for rollback): QUIT ends the session for both sides at the same frame,
 * NEWMAP is the online Backspace, a fresh round for both.  See DRB_CTRL_* in
 * destruct_rollback.h; pause is offline-only, so bit 0x02 stays unused. */

/* Bits a rollback prediction may repeat: the ones a player holds down.  Change-unit and the two
 * weapon cycles are edge triggered (DE_NetLocalActions consumes the key as it reads it), so
 * predicting one would take an action the peer never took. */
#define DE_ROLLBACK_HELD_ACTIONS ((1 << MOVE_LEFT) | (1 << MOVE_RIGHT) | (1 << MOVE_UP) \
                                  | (1 << MOVE_DOWN) | (1 << MOVE_FIRE))
#endif

static int de_round(float v)
{
	return (int)(v + (v >= 0.0f ? 0.5f : -0.5f));
}

static SDL_Surface* de_ensure_surface(SDL_Surface** surf, int scale)
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

static bool DE_ensureSmoothBuffers(int scale)
{
	return de_ensure_surface(&destruct_hi, scale) != NULL
	    && de_ensure_surface(&destruct_bg_hi, scale) != NULL;
}

static const char* const player_names[] =
{
	"left",
	"right",
};

static const char* const key_names[] =
{
	"left",
	"right",
	"up",
	"down",
	"change",
	"fire",
	"previous weapon",
	"next weapon",
};

static const char* const unit_names[] =
{
	"tank",
	"nuke",
	"dirt",
	"satellite",
	"magnet",
	"laser",
	"jumper",
	"heli",
};

static enum de_unit_t get_unit_by_name(const char* unit_name)
{
	for (enum de_unit_t unit = UNIT_FIRST; unit < MAX_UNITS; ++unit)
		if (strcmp(unit_name, unit_names[unit]) == 0)
			return unit;

	return UNIT_NONE;
}

static void load_destruct_config(Config* config_)
{
	ConfigSection* section;

	section = config_find_or_add_section(config_, "destruct", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory

	config.alwaysalias = config_get_or_set_bool_option(section, "antialias craters", false, NO_YES);

	weaponSystems[UNIT_LASER][SHOT_LASERTRACER] = config_get_or_set_bool_option(section, "tracer laser", false, OFF_ON);

	config.max_shots = config_get_or_set_int_option(section, "max shots", 40);
	config.max_explosions = config_get_or_set_int_option(section, "max explosions", 40);
	config.min_walls = config_get_or_set_int_option(section, "min walls", 20);
	config.max_walls = config_get_or_set_int_option(section, "max walls", 20);

	config.ai[0] = config_get_or_set_bool_option(section, "left ai", true, NO_YES);
	config.jumper_straight[0] = config_get_or_set_bool_option(section, "left jumper fires straight", true, NO_YES);
	config.ai[1] = config_get_or_set_bool_option(section, "right ai", false, NO_YES);
	config.jumper_straight[1] = config_get_or_set_bool_option(section, "right jumper fires straight", false, NO_YES);

	// keyboard controls

	for (int p = 0; p < MAX_PLAYERS; ++p)
	{
		section = config_find_section(config_, "destruct keyboard", player_names[p]);
		if (section == NULL)
			if ((section = config_add_section(config_, "destruct keyboard", player_names[p])) == NULL)
				exit(-1);

		ConfigOption* option;

		for (int k = 0; k < MAX_KEY; ++k)
		{
			if ((option = config_get_or_set_option(section, key_names[k], NULL)) == NULL)
				exit(-1);

			foreach_option_i_value(i, value, option)
			{
				SDL_Scancode key = SDL_GetScancodeFromName(value);
				if (key != SDL_SCANCODE_UNKNOWN && i < COUNTOF(defaultKeyConfig[p][k]))
				{
					defaultKeyConfig[p][k][i] = key;
				}
				else  // invalid or excess
				{
					foreach_remove_option_value();
					continue;
				}
			}

			if (config_get_value_count(option) > 0)
			{
				// unset remaining defaults
				for (unsigned int i = config_get_value_count(option); i < COUNTOF(defaultKeyConfig[p][k]); ++i)
					defaultKeyConfig[p][k][i] = SDL_SCANCODE_UNKNOWN;
			}
			else
			{
				// set defaults
				for (unsigned int i = 0; i < COUNTOF(defaultKeyConfig[p][k]); ++i)
					if (defaultKeyConfig[p][k][i] != SDL_SCANCODE_UNKNOWN)
						config_add_value(option, SDL_GetScancodeName(defaultKeyConfig[p][k][i]));
			}
		}
	}

	// custom destruct mode

	section = config_find_section(config_, "destruct custom", NULL);
	if (section == NULL)
		if ((section = config_add_section(config_, "destruct custom", NULL)) == NULL)
			exit(-1);

	config.allow_custom = config_get_or_set_bool_option(section, "enable", false, NO_YES);

	char buffer[15 + 1];

	for (int p = 0; p < MAX_PLAYERS; ++p)
	{
		snprintf(buffer, sizeof(buffer), "%s num units", player_names[p]);
		basetypes[8 + p][0] = config_get_or_set_int_option(section, buffer, basetypes[8 + p][0]);

		ConfigOption* option;

		snprintf(buffer, sizeof(buffer), "%s unit", player_names[p]);
		if ((option = config_get_or_set_option(section, buffer, NULL)) == NULL)
			exit(-1);

		foreach_option_i_value(i, value, option)
		{
			enum de_unit_t unit = get_unit_by_name(value);
			if (unit != UNIT_NONE && 1 + i < COUNTOF(basetypes[8 + p]))
			{
				basetypes[8 + p][1 + i] = unit;
			}
			else  // invalid or excess
			{
				foreach_remove_option_value();
				continue;
			}
		}

		if (config_get_value_count(option) > 0)
		{
			// set remaining units to tank
			for (unsigned int i = config_get_value_count(option); 1 + i < COUNTOF(basetypes[8 + p]); ++i)
			{
				basetypes[8 + p][1 + i] = UNIT_TANK;
				config_add_value(option, unit_names[UNIT_TANK]);
			}
		}
		else
		{
			// set defaults
			for (unsigned int i = 0; 1 + i < COUNTOF(basetypes[8 + p]); ++i)
				config_add_value(option, unit_names[basetypes[8 + p][1 + i]]);
		}
	}
}

#ifdef WITH_NETWORK
/* Pin every config value that affects Destruct simulation. Peers may have different local files;
 * the next offline game reloads its own values. */
static void DE_pinSessionConfig(void)
{
	config.max_shots = 40;
	config.max_explosions = 40;
	config.min_walls = 20;
	config.max_walls = 20;
	config.allow_custom = false;
	config.alwaysalias = false;
	config.ai[0] = config.ai[1] = false;
	config.jumper_straight[0] = config.jumper_straight[1] = true;
	weaponSystems[UNIT_LASER][SHOT_LASERTRACER] = false;
	/* max_installations bounds the unit-select wrap, so it is sim state too.  It is derived by
	 * the caller as a MAX over basetypes' counts, including the two custom army sizes this
	 * machine's config sets, and it never shrinks across visits, so pin the inputs AND the
	 * accumulator back to the shipped values. */
	basetypes[8][0] = basetypes[9][0] = 5;
	config.max_installations = 10;
}

/* One battle run headlessly with every frame replayed from its own snapshot; see
 * drb_selftest_tick.  Called in place of JE_destructMain, so it inherits the pools, sprites and
 * pinned config JE_destructGame has already set up. */
static void DE_SnapshotSelfTest(void)
{
	JE_loadPic(VGAScreen, 11, false);
	DE_widenHUDBackdrop(VGAScreen);

	DE_ResetPlayers();
	destruct_player[PLAYER_LEFT].is_cpu = false;
	destruct_player[PLAYER_RIGHT].is_cpu = false;
	world.destructMode = MODE_5CARDWAR;

	drb_selftest_arm(DE_StateSize(), DE_StateSave, DE_StateRestore, qa_destruct_selftest_ticks);

	while (drb_selftest_active())
	{
		destructFirstTime = true;
		mt_srand(0x0DE57121u + de_net_round++);
		DE_ResetUnits();
		DE_ResetLevel();
		drb_round_reset();

		/* A round that ends inside the budget rolls straight into the next one, so the run
		 * covers a round boundary as well as the battle itself. */
		while (drb_selftest_active() && DE_RunTick() == STATE_CONTINUE)
			;
	}

	printf("# destruct snapshot self-test: %lu ticks, %lu failures\n",
	       drb_selftest_ticks_run(), drb_selftest_failures());
}
#endif

void JE_destructGame(void)
{
	unsigned int i;

	set_menu_centered(false);
	JE_clr256(VGAScreen);
	JE_showVGA();

	load_destruct_config(&opentyrian_config);

#ifdef WITH_NETWORK
	de_net = isNetworkGame;
	// The host's Netcode row settles this for both machines, like every other online game type.
	de_net_rollback = de_net && nrb_session_mode();
	crashlog_set_phase(de_net ? "Destruct minigame (online)" : "Destruct minigame");
	if (de_net || qa_destruct_selftest_ticks > 0)
	{
		de_net_local_side = thisPlayerNum == 2 ? PLAYER_RIGHT : PLAYER_LEFT;
		de_net_round = 0;
		de_net_desync_noted = false;
		de_net_have_inputs = false;

		DE_pinSessionConfig();

		if (de_net)
			network_state_reset();
	}
#else
	crashlog_set_phase("Destruct minigame");
#endif

	/* A network teardown longjmps straight out of the tick loop, skipping the frees at the
	 * bottom; releasing the previous visit's buffers here keeps that path leak-free.  Disarming
	 * the rollback module belongs to the same rule, and doubly so: a visit that left it armed
	 * would have the next offline game reading itself as a rollback session. */
	destruct_deinit();

	//malloc things that have customizable sizes
	shotRec = malloc_die(sizeof(struct destruct_shot_s) * config.max_shots);
	exploRec = malloc_die(sizeof(struct destruct_explo_s) * config.max_explosions);
	world.mapWalls = malloc_die(sizeof(struct destruct_wall_s) * config.max_walls);

	//Malloc enough structures to cover all of this session's possible needs.
	for (i = 0; i < 10; i++)
		config.max_installations = MAX(config.max_installations, basetypes[i][0]);
	destruct_player[PLAYER_LEFT].unit = malloc_die(sizeof(struct destruct_unit_s) * config.max_installations);
	destruct_player[PLAYER_RIGHT].unit = malloc_die(sizeof(struct destruct_unit_s) * config.max_installations);

	destructTempScreen = game_screen;
	world.VGAScreen = VGAScreen;

#ifdef WITH_NETWORK
	/* Arm the snapshot ring now that the pools are allocated and the terrain buffer is known;
	 * like the buffers above, this also releases a ring a network teardown left behind. */
	if (de_net_rollback)
		drb_session_begin(DE_StateSize(), DE_StateSave, DE_StateRestore, DE_NetSimHash,
		                  DE_ROLLBACK_HELD_ACTIONS);
#endif

	JE_loadCompShapes(&destructSpriteSheet, '~');

	fade_black(1);

	destruct_sim_timing_init = false;   /* start the smooth-present clock fresh */

#ifdef WITH_NETWORK
	if (qa_destruct_selftest_ticks > 0)
		DE_SnapshotSelfTest();
	else
#endif
	JE_destructMain();

	destruct_deinit();
}

static void JE_destructMain(void)
{
	enum de_state_t curState;

	JE_loadPic(VGAScreen, 11, false);
	DE_widenHUDBackdrop(VGAScreen);
	JE_introScreen();

	DE_ResetPlayers();

	destruct_player[PLAYER_LEFT].is_cpu = config.ai[PLAYER_LEFT];
	destruct_player[PLAYER_RIGHT].is_cpu = config.ai[PLAYER_RIGHT];

	while (true)
	{
#ifdef WITH_NETWORK
		// Online, the battle was picked in the host's lobby and adopted from the connect
		// packet; there is no mode select to disagree on.  Clamped again here because it
		// indexes baseLookup/basetypes, and this is the last stop before it does.
		if (de_net)
			world.destructMode = (network_host_destruct_mode >= 0
			                      && network_host_destruct_mode < DESTRUCT_MODES)
			                   ? (enum de_mode_t)network_host_destruct_mode : MODE_5CARDWAR;
		else
#endif
		world.destructMode = JE_modeSelect();

		if (world.destructMode == MODE_NONE)
			break; /* User is quitting */

		do
		{

			destructFirstTime = true;
			JE_loadPic(VGAScreen, 11, false);
			DE_widenHUDBackdrop(VGAScreen);

#ifdef WITH_NETWORK
			// Every map derives from the shared session seed and the round number, so both
			// machines generate identical worlds (and pick the same song) without exchanging
			// a byte of them.
			if (de_net)
				mt_srand(network_destruct_session_seed + 0x9E3779B9u * de_net_round++);
#endif

			DE_ResetUnits();
			DE_ResetLevel();

			/* Each round is its own rollback timeline: frame 1, empty histories, and a fresh
			 * epoch so records still in flight from the round just finished are refused. */
			drb_round_reset();

			do
			{
				curState = DE_RunTick();
			} while (curState == STATE_CONTINUE);

			fade_black(25);
		} while (curState == STATE_RELOAD);

#ifdef WITH_NETWORK
		// An online session is one sitting: the quit verdict ended it on both machines at the
		// same tick, and the main loop tears the connection down from here.
		if (de_net)
			break;
#endif
	}
	set_menu_centered(true);
}

/* Composed from the backdrop stashed in VGAScreen2 rather than drawn straight over the screen:
 * online the two lines at the bottom track both players' readiness, so the whole title is redrawn
 * every frame.  The two flags mean nothing offline, where the screen is composed once. */
static void DE_composeIntro(bool localReady, bool peerReady)
{
	memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->h * VGAScreen->pitch);
	JE_outText(VGAScreen, center_text(specialName[SA_DESTRUCT - 1], TINY_FONT), 90, specialName[SA_DESTRUCT - 1], 12, 5);

#ifdef WITH_NETWORK
	// The joiner never saw the host's settings screen, so this is where both sides read what
	// the session is: the battle, the side this machine mans, and who is on the other one.
	if (de_net)
	{
		char line[64];

		snprintf(line, sizeof(line), "Online Destruct - %s",
		         destructModeName[(network_host_destruct_mode >= 0
		                           && network_host_destruct_mode < DESTRUCT_MODES)
		                          ? network_host_destruct_mode : 0]);
		JE_outText(VGAScreen, center_text(line, TINY_FONT), 110, line, 15, 4);

		snprintf(line, sizeof(line), "You command the %s side.",
		         de_net_local_side == PLAYER_LEFT ? "left" : "right");
		JE_outText(VGAScreen, center_text(line, TINY_FONT), 120, line, 15, 2);

		if (network_opponent_name[0] != '\0')
		{
			snprintf(line, sizeof(line), "Your opponent: %s", network_opponent_name);
			JE_outText(VGAScreen, center_text(line, TINY_FONT), 130, line, 15, 2);
		}

		// In place of the offline hints below: online neither F1 nor F10 does anything (the help
		// screen would stall the state stream, the AI toggle would fork the two sims), and this
		// screen is a barrier, so what belongs here is where the pair stands.
		const char* const own = localReady ? "You are ready."
		                                   : "Press any key when you are ready.";
		const char* const other = peerReady ? "The other player is ready."
		                                    : "Waiting for the other player...";
		// Esc is a step back rather than a way out while this player stands confirmed, so it takes
		// two presses to leave from there. Says which one it is about to be.
		const char* const leave = localReady ? "Esc takes back your ready."
		                                     : "Esc leaves the session.";
		JE_outText(VGAScreen, center_text(own, TINY_FONT), 170, own, localReady ? 12 : 15, 4);
		JE_outText(VGAScreen, center_text(other, TINY_FONT), 180, other, peerReady ? 12 : 15, 2);
		JE_outText(VGAScreen, center_text(leave, TINY_FONT), 190, leave, 15, 2);
		return;
	}
#endif

	(void)localReady;
	(void)peerReady;

	JE_outText(VGAScreen, center_text(miscText[64], TINY_FONT), 180, miscText[64], 15, 2);
	JE_outText(VGAScreen, center_text(miscText[65], TINY_FONT), 190, miscText[65], 15, 2);
}

#ifdef WITH_NETWORK
/* Online title barrier: both players confirm before the battle starts. Escape
 * first withdraws confirmation, then leaves; both states keep the link alive. */
static void DE_netIntroBarrier(void)
{
	bool localReady = false, peerReady = false;

	// Nothing counts until the press that opened this screen is let go: a key still down from the
	// lobby confirms the barrier before it can be read, and a held pad button auto-repeats into
	// fresh presses.  Bounded, so a drifting stick cannot lock the screen out instead; a deadline
	// of zero is the re-arm after a withdrawal, which waits for a real release (that key IS held).
	bool armed = false;
	Uint32 armDeadline = SDL_GetTicks() + 500;

	// A headless wire peer has nobody to press anything; it is ready as soon as it arrives.
	if (qa_net_gameplay_ticks > 0)
	{
		localReady = true;
		network_ready_publish(true);
	}

	while (true)
	{
		DE_composeIntro(localReady, peerReady);
		JE_showVGA();
		if (!output_vsync)
			limit_render_fps();

		watchdog_heartbeat();
		push_joysticks_as_keyboard();  // a controller confirms too (no keyboard on Switch)
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
				// Step back rather than out.  The peer is watching this line, so the withdrawal is
				// announced like the confirmation was; the release below waits on our own channel
				// being clear, and the channel is ordered, so a withdrawal sent before the peer's
				// confirmation was acknowledged always reaches them ahead of that acknowledgement.
				localReady = false;
				network_ready_publish(false);
				armed = false;
				armDeadline = 0;
			}
			else
			{
				// The way out, once nothing is standing behind it.  The other player is sitting on
				// this same screen and has to be told, or they wait out the dead-link timeout.
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
}
#endif

static void JE_introScreen(void)
{
	memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->h * VGAScreen2->pitch);

	DE_composeIntro(false, false);
	JE_showVGA();
	fade_palette(colors, 15, 0, 255);

	newkey = false;
#ifdef WITH_NETWORK
	if (de_net)
	{
		DE_netIntroBarrier();
	}
	else
#endif
	while (!newkey)
	{
		push_joysticks_as_keyboard();  // let a controller dismiss the title (no keyboard on Switch)
		service_SDL_events(false);
		touch_ui_idle_repaint();
		SDL_Delay(16);
	}

	fade_black(15);
	memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->h * VGAScreen->pitch);
	JE_showVGA();
}

static void DrawModeSelectMenu(enum de_mode_t mode)
{
	int i;

	for (i = 0; i < DESTRUCT_MODES; i++)
		JE_textShade(VGAScreen, center_text(destructModeName[i], TINY_FONT), 82 + i * 12, destructModeName[i], 12, (i == mode) * 4, FULL_SHADE);
	if (config.allow_custom == true)
		JE_textShade(VGAScreen, center_text("Custom", TINY_FONT), 82 + i * 12, "Custom", 12, (i == mode) * 4, FULL_SHADE);
}

static enum de_mode_t JE_modeSelect(void)
{
	enum de_mode_t mode;

	memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->h * VGAScreen2->pitch);
	mode = MODE_5CARDWAR;

	DrawModeSelectMenu(mode);

	JE_showVGA();
	fade_palette(colors, 15, 0, 255);

	while (true)
	{
		DrawModeSelectMenu(mode);
		JE_showVGA();

		newkey = false;
		do
		{
			push_joysticks_as_keyboard();  // controller -> arrows/Return/Escape (no keyboard on Switch)
			service_SDL_events(false);
			touch_ui_idle_repaint();
			SDL_Delay(16);
		} while (!newkey);

		if (keysactive[SDL_SCANCODE_ESCAPE])
		{
			mode = MODE_NONE;
			break;
		}
		if (keysactive[SDL_SCANCODE_RETURN])
		{
			break;
		}
		if (keysactive[SDL_SCANCODE_UP])
		{
			if (mode == MODE_FIRST)
			{
				if (config.allow_custom == true)
					mode = MODE_LAST;
				else
					mode = MODE_LAST - 1;
			}
			else
			{
				mode--;
			}
		}
		if (keysactive[SDL_SCANCODE_DOWN])
		{
			if (mode >= MODE_LAST - 1)
			{
				if (config.allow_custom == true && mode == MODE_LAST - 1)
					mode++;
				else
					mode = MODE_FIRST;
			}
			else
			{
				mode++;
			}
		}
	}

	fade_black(15);
	memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->h * VGAScreen->pitch);
	JE_showVGA();
	return mode;
}

static void JE_generateTerrain(void)
{
	/* Tall, fuzzy, and ring terrain are mutually exclusive. Walls and holes
	 * combine independently with the selected terrain shape. */

	world.mapFlags = MAP_NORMAL;

	if (mt_rand() % 2 == 0)
		world.mapFlags |= MAP_WALLS;
	if (mt_rand() % 4 == 0)
		world.mapFlags |= MAP_HOLES;
	switch (mt_rand() % 4)
	{
	case 0:
		world.mapFlags |= MAP_FUZZY;
		break;

	case 1:
		world.mapFlags |= MAP_TALL;
		break;

	case 2:
		world.mapFlags |= MAP_RINGS;
		break;
	}

	play_song(goodsel[mt_rand() % 14] - 1);

	/* JE_loadPic only fills the original 320px; the widescreen strip past it is
	 * never written, so clear that sky (rows below the HUD) to black or it shows
	 * stale pixels from the previous game that pile up across restarts.  Rows
	 * 0..HUD_ROWS-1 there belong to the right HUD frame, so leave them alone. */
	fill_rectangle_xy(VGAScreen, LEGACY_WIDTH, HUD_ROWS, vga_width - 1, vga_height - 1, PIXEL_BLACK);

	DE_generateBaseTerrain(world.mapFlags, world.baseMap);
	DE_generateUnits(world.baseMap);
	DE_generateWalls(&world);
	DE_drawBaseTerrain(world.baseMap);

	if (world.mapFlags & MAP_RINGS)
		DE_generateRings(world.VGAScreen, PIXEL_DIRT);
	if (world.mapFlags & MAP_HOLES)
		DE_generateRings(world.VGAScreen, PIXEL_BLACK);

	JE_aliasDirt(world.VGAScreen);
	JE_showVGA();

	memcpy(destructTempScreen->pixels, VGAScreen->pixels, destructTempScreen->pitch * destructTempScreen->h);
}

static void DE_generateBaseTerrain(unsigned int mapFlags, unsigned int* baseWorld)
{
	unsigned int i;
	unsigned int newheight, HeightMul;
	float sinewave, sinewave2, cosinewave, cosinewave2;

	/* Brown framebuffer pixels form the collision terrain. */

	 /* The ranges here are between .01 and roughly 0.07283...*/
	sinewave = mt_rand_lt1() * M_PI / 50 + 0.01f;
	sinewave2 = mt_rand_lt1() * M_PI / 50 + 0.01f;
	cosinewave = mt_rand_lt1() * M_PI / 50 + 0.01f;
	cosinewave2 = mt_rand_lt1() * M_PI / 50 + 0.01f;
	HeightMul = 20;

	/* This block just exists to mix things up. */
	if (mapFlags & MAP_FUZZY)
	{
		sinewave = M_PI - mt_rand_lt1() * 0.3f;
		sinewave2 = M_PI - mt_rand_lt1() * 0.3f;
	}
	if (mapFlags & MAP_TALL)
	{
		HeightMul = 100;
	}

	/* Now compute a height for each of our lines. */
	for (i = 1; i <= vga_width - 2; i++)
	{
		/* sim_ trig throughout the generator and the tick: the terrain is collision state and
		 * libm's sinf/cosf differ across platforms (see sim_math.h); an online PC<->console
		 * pair would grow different mountains from the same seed. */
		newheight = roundf(sim_sinf(sinewave * i) * HeightMul + sim_sinf(sinewave2 * i) * 15 +
			sim_cosf(cosinewave * i) * 10 + sim_sinf(cosinewave2 * i) * 15) + 130;

		/* Clamp the terrain height. */
		if (newheight < 40)
			newheight = 40;
		else if (newheight > 195)
			newheight = 195;
		baseWorld[i] = newheight;
	}
	/* The base world has been created. */
}

static void DE_drawBaseTerrain(unsigned int* baseWorld)
{
	unsigned int i;

	for (i = 1; i <= vga_width - 2; i++)
	{
		JE_rectangle(VGAScreen, i, baseWorld[i], i, 199, PIXEL_DIRT);
	}
}

static void DE_generateUnits(unsigned int* baseWorld)
{
	unsigned int i, j, numSatellites;

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		numSatellites = 0;
		destruct_player[i].unitsRemaining = 0;

		for (j = 0; j < basetypes[baseLookup[i][world.destructMode]][0]; j++)
		{
			/* Not everything is the same between players */
			if (i == PLAYER_LEFT)
			{
				destruct_player[i].unit[j].unitX = (mt_rand() % 120) + 10;
			}
			else
			{
				destruct_player[i].unit[j].unitX = vga_width - ((mt_rand() % 120) + 22);
			}

			destruct_player[i].unit[j].unitY = JE_placementPosition(destruct_player[i].unit[j].unitX - 1, 14, baseWorld);
			destruct_player[i].unit[j].unitType = basetypes[baseLookup[i][world.destructMode]][(mt_rand() % 10) + 1];

			/* Satellites do not count as active units. */
			if (destruct_player[i].unit[j].unitType == UNIT_SATELLITE)
			{
				if (numSatellites == basetypes[baseLookup[i][world.destructMode]][0])
				{
					destruct_player[i].unit[j].unitType = UNIT_TANK;
					destruct_player[i].unitsRemaining++;
				}
				else
				{
					/* Satellites keep their classic random altitude after terrain clearing. */
					destruct_player[i].unit[j].unitY = 30 + (mt_rand() % 40);
					numSatellites++;
				}
			}
			else
			{
				destruct_player[i].unitsRemaining++;
			}

			destruct_player[i].unit[j].lastMove = 0;
			destruct_player[i].unit[j].unitYMov = 0;
			destruct_player[i].unit[j].isYInAir = false;
			destruct_player[i].unit[j].angle = 0;
			destruct_player[i].unit[j].power = (destruct_player[i].unit[j].unitType == UNIT_LASER) ? 6 : 3;
			destruct_player[i].unit[j].shotType = defaultWeapon[destruct_player[i].unit[j].unitType];
			destruct_player[i].unit[j].health = baseDamage[destruct_player[i].unit[j].unitType];
			destruct_player[i].unit[j].ani_frame = 0;
			destruct_player[i].unit[j].prev_x = destruct_player[i].unit[j].unitX;
			destruct_player[i].unit[j].prev_y = destruct_player[i].unit[j].unitY;
		}
	}
}

static void DE_generateWalls(struct destruct_world_s* gameWorld)
{
	unsigned int i, j, wallX;
	unsigned int wallHeight, remainWalls;
	unsigned int tries;
	bool isGood;

	if ((world.mapFlags & MAP_WALLS) == false)
	{
		for (i = 0; i < config.max_walls; i++)
		{
			gameWorld->mapWalls[i].wallExist = false;
		}
		return;
	}

	/* mt_rand, not libc rand(): the wall count is part of the map, and online both machines
	 * must draw it from the seeded generator (libc's stream is never reseeded in step). */
	remainWalls = (mt_rand() % (config.max_walls - config.min_walls + 1)) + config.min_walls;

	do
	{
		wallHeight = (mt_rand() % 5) + 1;
		if (wallHeight > remainWalls)
		{
			wallHeight = remainWalls;
		}

		tries = 0;
		do
		{
			isGood = true;
			wallX = (mt_rand() % (vga_width - 20)) + 10;

			/* Try four unobstructed positions, then accept overlap so generation
			 * always terminates. */
			for (i = 0; i < MAX_PLAYERS; i++)
			{
				for (j = 0; j < config.max_installations; j++)
				{
					if ((wallX > destruct_player[i].unit[j].unitX - 12) &&
						(wallX < destruct_player[i].unit[j].unitX + 13))
					{
						isGood = false;
						goto label_outer_break;
					}
				}
			}

		label_outer_break:
			tries++;

		} while (isGood == false && tries < 5);

		/* Cap walls below the HUD sky window and leave two blocks of headroom. The
		 * MAX also guarantees remainWalls shrinks if the terrain clamp changes. */
		{
			const unsigned int baseY = JE_placementPosition(wallX, 12, gameWorld->baseMap);
			const unsigned int headroom = MAX((baseY - HUD_ROWS) / 14, 1u);
			if (wallHeight > headroom)
				wallHeight = headroom;

			for (i = 1; i <= wallHeight; i++)
			{
				gameWorld->mapWalls[remainWalls - i].wallExist = true;
				gameWorld->mapWalls[remainWalls - i].wallX = wallX;
				gameWorld->mapWalls[remainWalls - i].wallY = baseY - 14 * i;
			}
		}

		remainWalls -= wallHeight;

	} while (remainWalls != 0);
}

static void DE_generateRings(SDL_Surface* screen, Uint8 pixel)
{
	unsigned int i, j, tempSize, rings;
	int tempPosX1, tempPosY1, tempPosX2, tempPosY2;
	float tempRadian;

	rings = mt_rand() % 6 + 1;
	for (i = 1; i <= rings; i++)
	{
		tempPosX1 = (mt_rand() % vga_width);
		tempPosY1 = (mt_rand() % 160) + 20;
		tempSize = (mt_rand() % 40) + 10;  /*Size*/

		for (j = 1; j <= tempSize * tempSize * 2; j++)
		{
			tempRadian = mt_rand_lt1() * (2 * M_PI);
			tempPosY2 = tempPosY1 + roundf(sim_cosf(tempRadian) * (mt_rand_lt1() * 0.1f + 0.9f) * tempSize);
			tempPosX2 = tempPosX1 + roundf(sim_sinf(tempRadian) * (mt_rand_lt1() * 0.1f + 0.9f) * tempSize);
			if ((tempPosY2 > 12) && (tempPosY2 < 200) &&
				(tempPosX2 > 0) && (tempPosX2 < vga_width - 1))
			{
				((Uint8*)screen->pixels)[tempPosX2 + tempPosY2 * screen->pitch] = pixel;
			}
		}
	}
}

static unsigned int aliasDirtPixel(const SDL_Surface* screen, unsigned int x, unsigned int y, const Uint8* s)
{
	// Keep the dirt-aliasing neighborhood lookup local to this helper.
	unsigned int newColor = PIXEL_BLACK;

	if ((y > 0) && (*(s - screen->pitch) == PIXEL_DIRT)) // look up
		newColor += 1;
	if ((y < screen->h - 1u) && (*(s + screen->pitch) == PIXEL_DIRT)) // look down
		newColor += 3;
	if ((x > 0) && (*(s - 1) == PIXEL_DIRT)) // look left
		newColor += 2;
	if ((x < screen->pitch - 1u) && (*(s + 1) == PIXEL_DIRT)) // look right
		newColor += 2;
	if (newColor != PIXEL_BLACK)
		return newColor + 16; // 16 must be the start of the brown pixels.

	return PIXEL_BLACK;
}

static void JE_aliasDirt(SDL_Surface* screen)
{
	/* This complicated looking function goes through the whole screen
	 * looking for brown pixels which just happen to be next to non-brown
	 * pixels.  It's an aliaser, just like it says. */
	unsigned int x, y;

	Uint8* s = screen->pixels;
	s += 12 * screen->pitch;

	for (y = 12; y < (unsigned int)screen->h; y++)
	{
		for (x = 0; x < (unsigned int)screen->pitch; x++)
		{
			if (*s == PIXEL_BLACK)
				*s = aliasDirtPixel(screen, x, y, s);

			s++;
		}
	}
}

static void DE_widenHUDBackdrop(SDL_Surface* surface)
{
	/* HUD backdrop = top 12 rows of pic #11: two 320px box frames pinned flush to each screen
	 * edge with the widened middle blacked out. Finished strip stashed in hudBackdrop for per-tick
	 * repaints. */
	enum
	{
		LEFT_SRC_X  = 2,    /* left frame's authored x in pic #11 */
		RIGHT_SRC_X = 172   /* right frame's authored x in pic #11 */
	};

	if (surface->w < vga_width || surface->h < HUD_ROWS || surface->pitch < vga_width)
		return;

	for (int y = 0; y < HUD_ROWS; ++y)
	{
		Uint8* row = (Uint8*)surface->pixels + y * surface->pitch;

		memmove(row + HUD_FRAME_LEFT_X,  row + LEFT_SRC_X,  HUD_FRAME_W);  /* left frame  -> flush left  */
		memmove(row + HUD_FRAME_RIGHT_X, row + RIGHT_SRC_X, HUD_FRAME_W);  /* right frame -> flush right */
		memset(row + HUD_FRAME_LEFT_X + HUD_FRAME_W, PIXEL_BLACK,
		       HUD_FRAME_RIGHT_X - (HUD_FRAME_LEFT_X + HUD_FRAME_W));      /* clear the middle */

		memcpy(hudBackdrop + y * vga_width, row, vga_width);
	}
}

static unsigned int JE_placementPosition(unsigned int passed_x, unsigned int width, unsigned int* world)
{
	unsigned int i, new_y;

	/* Clamp the wall footprint before reading baseMap. The widest wall can otherwise
	 * reach one column past the terrain array and corrupt the next draw. */
	const unsigned int last_x = MIN(passed_x + width - 1, (unsigned int)vga_width - 1);

	/* Flatten the unit footprint to its highest terrain column. This preserves
	 * the large clearings produced for elevated units. */
	new_y = 0;
	for (i = passed_x; i <= last_x; i++)
	{
		if (new_y < world[i])
			new_y = world[i];
	}

	for (i = passed_x; i <= last_x; i++)
	{
		world[i] = new_y;
	}

	return new_y;
}

static bool JE_stabilityCheck(unsigned int x, unsigned int y)
{
	unsigned int i, numDirtPixels;
	Uint8* s;

	numDirtPixels = 0;
	s = destructTempScreen->pixels;
	s += x + (y * destructTempScreen->pitch) - 1;

	/* Check the 12 pixels on the bottom border of our object */
	for (i = 0; i < 12; i++)
	{
		if (*s == PIXEL_DIRT)
			numDirtPixels++;

		s++;
	}

	/* Fewer than ten brown pixels do not form a solid base. */
	return (numDirtPixels < 10);
}

static void DE_blendTempPixel(int x, int y)
{
	/* Fade any explosion pixel (palette 241..255 fades dark-red -> bright-yellow),
	 * optionally alias dirt, then copy the temp-screen pixel to VGAScreen.  Shared
	 * by the playfield and HUD-gap passes of JE_tempScreenChecking. */
	Uint8* temps = (Uint8*)destructTempScreen->pixels + y * destructTempScreen->pitch + x;

	if (*temps >= 241)
		*temps = (*temps == 241) ? PIXEL_BLACK : *temps - 1;

	if (config.alwaysalias == true && *temps == PIXEL_BLACK)
		*temps = aliasDirtPixel(VGAScreen, x, y, temps);

	((Uint8*)VGAScreen->pixels)[y * VGAScreen->pitch + x] = *temps;
}

static void JE_tempScreenChecking(void) /*and copy to vgascreen*/
{
	/* The playfield is everything from row HUD_ROWS down (full width). */
	for (int y = HUD_ROWS; y < VGAScreen->h; y++)
		for (int x = 0; x < VGAScreen->pitch; x++)
			DE_blendTempPixel(x, y);

	/* The gap between the two HUD boxes is live playfield as well, all the way to
	 * the top of the screen, so gameplay passing through it isn't clipped against
	 * the black HUD strip. */
	for (int y = 0; y < HUD_ROWS; y++)
		for (int x = HUD_GAP_LEFT; x < HUD_FRAME_RIGHT_X; x++)
			DE_blendTempPixel(x, y);
}

static void JE_makeExplosion(unsigned int tempPosX, unsigned int tempPosY, enum de_shot_t shottype)
{
	unsigned int i, tempExploSize;

	/* Find an available explosion slot. */
	for (i = 0; i < config.max_explosions; i++)
		if (exploRec[i].isAvailable == true)
			break;
	if (i == config.max_explosions) /* No empty slots */
		return;

	exploRec[i].isAvailable = false;
	exploRec[i].x = tempPosX;
	exploRec[i].y = tempPosY;
	exploRec[i].explowidth = 2;

	if (shottype != SHOT_INVALID)
	{
		tempExploSize = exploSize[shottype];
		if (tempExploSize < 5)
			JE_eSound(3);
		else if (tempExploSize < 15)
			JE_eSound(4);
		else if (tempExploSize < 20)
			JE_eSound(12);
		else if (tempExploSize < 40)
			JE_eSound(11);
		else
		{
			JE_eSound(12);
			JE_eSound(11);
		}

		exploRec[i].explomax = tempExploSize;
		exploRec[i].explofill = exploDensity[shottype];
		exploRec[i].exploType = shotDirt[shottype];
	}
	else
	{
		JE_eSound(4);
		exploRec[i].explomax = (mt_rand() % 40) + 10;
		exploRec[i].explofill = (mt_rand() % 60) + 20;
		exploRec[i].exploType = EXPL_NORMAL;
	}
}

static void JE_eSound(unsigned int sound)
{
	static int exploSoundChannel = 0;

	if (++exploSoundChannel > 5)
		exploSoundChannel = 1;

	soundQueue[exploSoundChannel] = sound;
}

static void JE_superPixel(unsigned int tempPosX, unsigned int tempPosY)
{
	const unsigned int starPattern[5][5] =
	{
		{   0,   0, 246,   0,   0 },
		{   0, 247, 249, 247,   0 },
		{ 246, 249, 252, 249, 246 },
		{   0, 247, 249, 247,   0 },
		{   0,   0, 246,   0,   0 }
	};
	const unsigned int starIntensity[5][5] =
	{
		{   0,   0,   1,   0,   0 },
		{   0,   1,   2,   1,   0 },
		{   1,   2,   4,   2,   1 },
		{   0,   1,   2,   1,   0 },
		{   0,   0,   1,   0,   0 }
	};

	/* Each pixel is addressed from its own clipped coordinates.  A walking pointer cannot do it:
	 * the star starts two rows and two columns back, so a flare near an edge underflows the
	 * unsigned start offset, and skipping a clipped row leaves the pointer short by the five
	 * columns that row would have stepped -- every row after it lands five pixels to the left. */
	const int maxX = destructTempScreen->pitch;
	const int maxY = destructTempScreen->h;
	Uint8* const pixels = destructTempScreen->pixels;

	for (int y = 0; y < 5; y++)
	{
		const int py = (int)tempPosY + y - 2;
		if (py < 0 || py >= maxY)
			continue;

		for (int x = 0; x < 5; x++)
		{
			const int px = (int)tempPosX + x - 2;
			if (px < 0 || px >= maxX)
				continue;

			if (starPattern[y][x] == 0)
				continue;  /* this is just to speed it up */

			/* at this point *s is our pixel.  Our constant arrays tell us what
			 * to do with it. */
			Uint8* const s = pixels + (size_t)py * maxX + px;
			if (*s < starPattern[y][x])
				*s = starPattern[y][x];
			else if (*s + starIntensity[y][x] > 255)
				*s = 255;
			else
				*s += starIntensity[y][x];
		}
	}
}

static void JE_helpScreen(void)
{
	unsigned int i, j;

	//JE_getVGA();  didn't do anything anyway?
	fade_black(15);
	memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->h * VGAScreen2->pitch);
	JE_clr256(VGAScreen);

	for (i = 0; i < 2; i++)
	{
		JE_outText(VGAScreen, 100, 5 + i * 90, destructHelp[i * 12 + 0], 2, 4);
		JE_outText(VGAScreen, 100, 15 + i * 90, destructHelp[i * 12 + 1], 2, 1);
		for (j = 3; j <= 12; j++)
			JE_outText(VGAScreen, ((j - 1) % 2) * 160 + 10, 15 + ((j - 1) / 2) * 12 + i * 90, destructHelp[i * 12 + j - 1], 1, 3);
	}
	JE_outText(VGAScreen, 30, 190, destructHelp[24], 3, 4);
	JE_showVGA();
	fade_palette(colors, 15, 0, 255);

	do  /* wait until user hits a key */
	{
		push_joysticks_as_keyboard();  // controller counts as a keypress (no keyboard on Switch)
		service_SDL_events(true);
		touch_ui_idle_repaint();
		SDL_Delay(16);
	} while (!newkey);

	fade_black(15);
	memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->h * VGAScreen->pitch);
	JE_showVGA();
	fade_palette(colors, 15, 0, 255);
}

static void JE_pauseScreen(void)
{
	set_volume(tyrMusicVolume / 2, fxVolume);

	/* Save our current screen/game world.  We don't want to screw it up while paused. */
        memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->h * VGAScreen2->pitch);
        JE_outText(VGAScreen, center_text(miscText[22], TINY_FONT), 90, miscText[22], 12, 5);
	JE_showVGA();

	do  /* wait until user hits a key */
	{
		push_joysticks_as_keyboard();  // controller counts as a keypress (no keyboard on Switch)
		service_SDL_events(true);
		touch_ui_idle_repaint();
		SDL_Delay(16);
	} while (!newkey);

	/* Restore current screen & volume*/
	memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->h * VGAScreen->pitch);
	JE_showVGA();

	set_volume(tyrMusicVolume, fxVolume);
}

/* DE_ResetX
 *
 * The reset functions clear the state of whatever they are assigned to.
 */
static void DE_ResetUnits(void)
{
	unsigned int p, u;

	for (p = 0; p < MAX_PLAYERS; ++p)
		for (u = 0; u < config.max_installations; ++u)
			destruct_player[p].unit[u].health = 0;
}

static void DE_ResetPlayers(void)
{
	unsigned int i;

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		destruct_player[i].is_cpu = false;
		destruct_player[i].unitSelected = 0;
		destruct_player[i].shotDelay = 0;
		destruct_player[i].score = 0;
		destruct_player[i].aiMemory.c_Angle = 0;
		destruct_player[i].aiMemory.c_Power = 0;
		destruct_player[i].aiMemory.c_Fire = 0;
		destruct_player[i].aiMemory.c_noDown = 0;
		memcpy(destruct_player[i].keys.Config, defaultKeyConfig[i], sizeof(destruct_player[i].keys.Config));
	}
}

static void DE_ResetWeapons(void)
{
	unsigned int i;

	for (i = 0; i < config.max_shots; i++)
		shotRec[i].isAvailable = true;

	for (i = 0; i < config.max_explosions; i++)
		exploRec[i].isAvailable = true;
}

static void DE_ResetLevel(void)
{
	/* Okay, let's prep the arena */

	DE_ResetWeapons();

	JE_generateTerrain();
	DE_ResetAI();
}

static void DE_ResetAI(void)
{
	unsigned int i, j;
	struct destruct_unit_s* ptr;

	for (i = PLAYER_LEFT; i < MAX_PLAYERS; i++)
	{
		if (destruct_player[i].is_cpu == false)
			continue;
		ptr = destruct_player[i].unit;

		for (j = 0; j < config.max_installations; j++, ptr++)
		{
			if (DE_isValidUnit(ptr) == false)
				continue;

			if (systemAngle[ptr->unitType] || ptr->unitType == UNIT_HELI)
				ptr->angle = M_PI_4;
			else
				ptr->angle = 0;

			ptr->power = (ptr->unitType == UNIT_LASER) ? 6 : 4;

			if (world.mapFlags & MAP_WALLS)
				ptr->shotType = defaultCpuWeaponB[ptr->unitType];
			else
				ptr->shotType = defaultCpuWeapon[ptr->unitType];
		}
	}
}

static void DE_ResetActions(void)
{
	unsigned int i;

	for (i = 0; i < MAX_PLAYERS; i++)
	{	/* Zero it all.  A memset would do the trick */
		memset(&(destruct_player[i].moves), 0, sizeof(destruct_player[i].moves));
	}
}

/* Smooth present helpers: build a supersampled (NxN) frame from the same game state as the
 * classic per-tick draw, with moving objects at interpolated sub-pixel positions. The static
 * terrain expands once per tick into destruct_bg_hi; each frame copies it, then draws the
 * interpolated foreground and HUD on top. */

/* Fill a scale x scale block at hi-surface pixel (hx, hy), clipped to the surface. */
static void DE_pixScaled(SDL_Surface* hi, int hx, int hy, Uint8 c, int scale)
{
	int x0 = hx < 0 ? 0 : hx, y0 = hy < 0 ? 0 : hy;
	int x1 = hx + scale, y1 = hy + scale;
	if (x1 > hi->w) x1 = hi->w;
	if (y1 > hi->h) y1 = hi->h;

	for (int y = y0; y < y1; ++y)
	{
		Uint8* p = (Uint8*)hi->pixels + y * hi->pitch + x0;
		for (int x = x0; x < x1; ++x)
			*p++ = c;
	}
}

/* Supersampled equivalent of JE_pixCool: a bright centre with four dimmer
 * neighbours, each a scale x scale block one logical pixel (= scale) apart. */
static void DE_pixCoolScaled(SDL_Surface* hi, int hx, int hy, Uint8 c, int scale)
{
	DE_pixScaled(hi, hx,         hy,         c,     scale);
	DE_pixScaled(hi, hx - scale, hy,         c - 2, scale);
	DE_pixScaled(hi, hx + scale, hy,         c - 2, scale);
	DE_pixScaled(hi, hx,         hy - scale, c - 2, scale);
	DE_pixScaled(hi, hx,         hy + scale, c - 2, scale);
}

/* Block-expand the clean terrain (VGAScreen right after JE_tempScreenChecking)
 * into destruct_bg_hi.  Runs once per tick; the terrain is static within a tick.
 * The HUD box rows are stale here but get repainted on top per frame (DE_ExpandHUD). */
static void DE_ExpandBackgroundHi(int scale)
{
	for (int y = 0; y < vga_height; ++y)
	{
		const Uint8* src = (const Uint8*)VGAScreen->pixels + y * VGAScreen->pitch;
		Uint8* hrow = (Uint8*)destruct_bg_hi->pixels + (y * scale) * destruct_bg_hi->pitch;

		for (int x = 0; x < vga_width; ++x)
		{
			Uint8 c = src[x];
			Uint8* p = hrow + x * scale;
			for (int xx = 0; xx < scale; ++xx)
				p[xx] = c;
		}
		for (int yy = 1; yy < scale; ++yy)
			memcpy(hrow + yy * destruct_bg_hi->pitch, hrow, (size_t)vga_width * scale);
	}
}

/* Repaint the two HUD boxes (rows 0..HUD_ROWS-1, the flush-mounted left/right
 * frames) on top of the composed frame, block-expanded from the 1x HUD that
 * DE_RunTickDrawHUD drew into VGAScreen this tick.  The gap between the boxes is
 * live playfield and is left untouched, matching the classic path. */
static void DE_ExpandHUD(SDL_Surface* hi, int scale)
{
	static const int spans[2][2] = { { 0, HUD_GAP_LEFT }, { HUD_FRAME_RIGHT_X, vga_width } };

	for (int y = 0; y < HUD_ROWS; ++y)
	{
		const Uint8* src = (const Uint8*)VGAScreen->pixels + y * VGAScreen->pitch;
		Uint8* hrow = (Uint8*)hi->pixels + (y * scale) * hi->pitch;

		for (int s = 0; s < 2; ++s)
			for (int x = spans[s][0]; x < spans[s][1]; ++x)
			{
				Uint8 c = src[x];
				Uint8* p = hrow + x * scale;
				for (int xx = 0; xx < scale; ++xx)
					p[xx] = c;
			}

		for (int yy = 1; yy < scale; ++yy)
			for (int s = 0; s < 2; ++s)
				memcpy(hrow + yy * hi->pitch + spans[s][0] * scale,
				       hrow + spans[s][0] * scale,
				       (size_t)(spans[s][1] - spans[s][0]) * scale);
	}
}

static void DE_DrawWallsScaled(SDL_Surface* hi, int scale)
{
	for (unsigned int i = 0; i < config.max_walls; ++i)
		if (world.mapWalls[i].wallExist)
			blit_sprite2_scaled(hi, world.mapWalls[i].wallX * scale, world.mapWalls[i].wallY * scale,
			                    destructSpriteSheet, 42, scale, BLIT2_COPY, 0);
}

static void DE_DrawUnitsScaled(SDL_Surface* hi, int scale, float alpha)
{
	for (unsigned int p = 0; p < MAX_PLAYERS; ++p)
	{
		struct destruct_unit_s* unit = destruct_player[p].unit;
		for (unsigned int u = 0; u < config.max_installations; ++u, ++unit)
		{
			if (DE_isValidUnit(unit) == false)
				continue;

			float ix = unit->prev_x + ((float)unit->unitX - unit->prev_x) * alpha;
			float iy = unit->prev_y + (unit->unitY - unit->prev_y) * alpha;

			blit_sprite2_scaled(hi, de_round(ix * scale), de_round(iy * scale) - 13 * scale,
			                    destructSpriteSheet, DE_unitAnimIndex(p, unit), scale, BLIT2_COPY, 0);
		}
	}
}

static void DE_DrawShotsScaled(SDL_Surface* hi, int scale, float alpha)
{
	for (unsigned int i = 0; i < config.max_shots; ++i)
	{
		if (shotRec[i].isAvailable)
			continue;

		/* Match the tick draw: hide both head and frozen trails while the shot is
		 * offscreen, preventing stale trail pixels in the sky window. */
		if (shotRec[i].y < 0 || shotRec[i].y >= vga_height)
			continue;

		Uint8 headColor = (shotColor[shotRec[i].shottype] << 4) - 3;

		/* Trail: historical positions, drawn where the sim left them (no interpolation). */
		for (int t = 0; t < 4; ++t)
			if (shotRec[i].trailc[t] > 0 && shotRec[i].traily[t] > 0)
				DE_pixCoolScaled(hi, shotRec[i].trailx[t] * scale, shotRec[i].traily[t] * scale,
				                 shotRec[i].trailc[t], scale);

		/* Head: interpolated from last tick's position to this tick's. */
		float ix = shotRec[i].prev_x + (shotRec[i].x - shotRec[i].prev_x) * alpha;
		float iy = shotRec[i].prev_y + (shotRec[i].y - shotRec[i].prev_y) * alpha;
		if (iy >= 0 && iy < vga_height)
			DE_pixCoolScaled(hi, de_round(ix * scale), de_round(iy * scale), headColor, scale);
	}
}

static void DE_DrawCrosshairsScaled(SDL_Surface* hi, int scale, float alpha)
{
	/* Mirrors DE_RunTickDrawCrosshairs, but off the interpolated unit position and
	 * drawn as scaled blocks. */
	for (unsigned int i = 0; i < MAX_PLAYERS; ++i)
	{
		int direction = (i == PLAYER_LEFT) ? -1 : 1;
		struct destruct_unit_s* curUnit = &destruct_player[i].unit[destruct_player[i].unitSelected];

		float ux = curUnit->prev_x + ((float)curUnit->unitX - curUnit->prev_x) * alpha;
		float uy = curUnit->prev_y + (curUnit->unitY - curUnit->prev_y) * alpha;

		float fx, fy;
		if (curUnit->unitType == UNIT_HELI)
		{
			fx = ux + 0.1f * curUnit->lastMove * curUnit->lastMove * curUnit->lastMove + 5;
			fy = uy + 1;
		}
		else
		{
			fx = ux + 6 - cosf(curUnit->angle) * (curUnit->power * 8 + 7) * direction;
			fy = uy - 7 - sinf(curUnit->angle) * (curUnit->power * 8 + 7);
		}

		int tempPosY = de_round(fy);   /* logical Y, for the HUD-clip gates below */
		int hx = de_round(fx * scale);
		int hy = de_round(fy * scale);

		if (tempPosY > 9)
		{
			if (tempPosY > 11)
			{
				if (tempPosY > 13)
					DE_pixScaled(hi, hx, hy - 2 * scale, 3, scale);   /* top pixel */
				DE_pixScaled(hi, hx + 3 * scale, hy, 3, scale);
				DE_pixScaled(hi, hx,             hy, 14, scale);
				DE_pixScaled(hi, hx - 3 * scale, hy, 3, scale);
			}
			DE_pixScaled(hi, hx, hy + 2 * scale, 3, scale);           /* bottom pixel */
		}
	}
}

static void DE_ComposeFrame(SDL_Surface* hi, int scale, float alpha)
{
	memcpy(hi->pixels, destruct_bg_hi->pixels, (size_t)hi->h * hi->pitch);
	DE_DrawWallsScaled(hi, scale);
	DE_DrawUnitsScaled(hi, scale, alpha);
	DE_DrawShotsScaled(hi, scale, alpha);
	DE_DrawCrosshairsScaled(hi, scale, alpha);
	DE_ExpandHUD(hi, scale);   /* HUD last, on top, matching the classic draw order */
}

/* Present interpolated, supersampled frames until a full tick period has elapsed,
 * then return so the caller runs the next simulation tick.  Modeled on the main
 * game's present loop (JE_starShowVGA): a real-time accumulator keeps the sim rate
 * exact regardless of how many frames we manage to draw. */
static void DE_SmoothPresent(int scale)
{
	const float period = get_delay_period();   /* destruct paces one tick per setDelay(1) unit */

	if (!destruct_sim_timing_init)
	{
		destruct_sim_freq = SDL_GetPerformanceFrequency();
		destruct_sim_last = SDL_GetPerformanceCounter();
		destruct_sim_accum = 0.0f;
		destruct_sim_timing_init = true;
	}

	const float counter_to_ms = 1000.0f / (float)destruct_sim_freq;

	for (;;)
	{
		const Uint64 now = SDL_GetPerformanceCounter();
		float elapsed = (float)(now - destruct_sim_last) * counter_to_ms;
		destruct_sim_last = now;
		if (elapsed > period * 4.0f)
			elapsed = period;   /* spiral guard (lag spike / resume from pause) */
		destruct_sim_accum += elapsed;

		if (destruct_sim_accum >= period)
		{
			destruct_sim_accum -= period;
			if (destruct_sim_accum > period)
				destruct_sim_accum = period;   /* at most one tick behind */
			break;
		}

		DE_ComposeFrame(destruct_hi, scale, destruct_sim_accum / period);
		present_hi(destruct_hi);

		if (!output_vsync)
			limit_render_fps();
		service_SDL_events(false);
	}

	setDelay(1);   /* keep `target` current for other timing readers */
}

/* Action bits from the on-screen buttons, shaped like DE_NetLocalActions so both the
 * offline and the online gather can fold them in. Holding a direction repeats through the
 * per-tick read, exactly as a held key does; the two cyclers are taps and are consumed
 * here, so this runs once per live tick and never during a re-simulation. */
static Uint8 DE_TouchActions(void)
{
	Uint8 bits = 0;

	if (touch_ui_held(TOUCH_BTN_LEFT))       bits |= 1 << KEY_LEFT;
	if (touch_ui_held(TOUCH_BTN_RIGHT))      bits |= 1 << KEY_RIGHT;
	if (touch_ui_held(TOUCH_BTN_UP))         bits |= 1 << KEY_UP;
	if (touch_ui_held(TOUCH_BTN_DOWN))       bits |= 1 << KEY_DOWN;
	if (touch_ui_held(TOUCH_BTN_FIRE))       bits |= 1 << KEY_FIRE;
	if (touch_ui_take_tap(TOUCH_BTN_CHANGE)) bits |= 1 << KEY_CHANGE;
	if (touch_ui_take_tap(TOUCH_BTN_CYCLE))  bits |= 1 << KEY_CYUP;

	return bits;
}

#ifdef WITH_NETWORK

/* Local action bits for this tick.  Online, BOTH keyboard layouts drive the local side --
 * whichever side that is -- so the arrow-key layout and the CVAZ layout both work, and the pad
 * maps to the local side alone.  Edge-triggered keys are consumed exactly as offline. */
static Uint8 DE_NetLocalActions(void)
{
	Uint8 bits = 0;

	for (unsigned int key_index = 0; key_index < MAX_KEY; key_index++)
	{
		for (unsigned int player_index = 0; player_index < MAX_PLAYERS; player_index++)
		{
			for (unsigned int slot_index = 0; slot_index < MAX_KEY_OPTIONS; slot_index++)
			{
				const SDL_Scancode key = destruct_player[player_index].keys.Config[key_index][slot_index];
				if (key == SDL_SCANCODE_UNKNOWN)
					break;
				if (keysactive[key])
				{
					bits |= 1 << key_index;
					if (key_index == KEY_CHANGE || key_index == KEY_CYUP || key_index == KEY_CYDN)
						keysactive[key] = false;
				}
			}
		}
	}

	if (joysticks > 0)
	{
		poll_joystick(0);

		if (joystick[0].direction[3]) bits |= 1 << KEY_LEFT;
		if (joystick[0].direction[1]) bits |= 1 << KEY_RIGHT;
		if (joystick[0].direction[0]) bits |= 1 << KEY_UP;
		if (joystick[0].direction[2]) bits |= 1 << KEY_DOWN;
		// Same dual-bound guard as the offline pad mapping: no firing mid-cycle.
		const bool cycling = joystick[0].action[2] || joystick[0].action[3];
		if (joystick[0].action[0] && !cycling) bits |= 1 << KEY_FIRE;
		if (joystick[0].action_pressed[1]) bits |= 1 << KEY_CHANGE;
		if (joystick[0].action_pressed[2]) bits |= 1 << KEY_CYDN;
		if (joystick[0].action_pressed[3]) bits |= 1 << KEY_CYUP;
		// Pad pause = leave, like offline (no keyboard on the consoles); routed through the
		// quit control bit by the gather below.
		if (joystick[0].action_pressed[5]) keysactive[SDL_SCANCODE_ESCAPE] = true;
	}

	bits |= DE_TouchActions();

	return bits;
}

// Consume session keys before the offline handlers can act on them again.
static Uint8 DE_NetLocalControls(void)
{
	Uint8 bits = 0;

	if (keysactive[SDL_SCANCODE_ESCAPE])
	{
		keysactive[SDL_SCANCODE_ESCAPE] = false;
		bits |= DRB_CTRL_QUIT;
	}
	if (keysactive[SDL_SCANCODE_BACKSPACE])
	{
		keysactive[SDL_SCANCODE_BACKSPACE] = false;
		bits |= DRB_CTRL_NEWMAP;
	}

	return bits;
}

static Uint32 de_net_hash_u32(Uint32 h, Uint32 v)
{
	return (h ^ v) * 16777619u;
}

static Uint32 de_net_float_bits(float f)
{
	Uint32 u;
	memcpy(&u, &f, sizeof(u));
	return u;
}

/* Desync canary: a summary of everything both netcodes are supposed to keep identical.  Pixel
 * state (the dirt) is left out as too expensive per tick; a divergence there moves a unit or
 * shot within a few ticks and lands in here anyway. */
static Uint32 DE_NetSimHash(void)
{
	Uint32 h = 2166136261u;

	for (unsigned int p = 0; p < MAX_PLAYERS; ++p)
	{
		const struct destruct_player_s* pl = &destruct_player[p];
		h = de_net_hash_u32(h, pl->unitsRemaining);
		h = de_net_hash_u32(h, pl->unitSelected);
		h = de_net_hash_u32(h, pl->shotDelay);
		h = de_net_hash_u32(h, pl->score);

		for (unsigned int u = 0; u < config.max_installations; ++u)
		{
			const struct destruct_unit_s* unit = &pl->unit[u];
			if (unit->health <= 0)
				continue;
			h = de_net_hash_u32(h, unit->unitX);
			h = de_net_hash_u32(h, de_net_float_bits(unit->unitY));
			h = de_net_hash_u32(h, (Uint32)unit->health);
			h = de_net_hash_u32(h, de_net_float_bits(unit->angle));
			h = de_net_hash_u32(h, de_net_float_bits(unit->power));
			h = de_net_hash_u32(h, (Uint32)unit->unitType);
			h = de_net_hash_u32(h, (Uint32)unit->shotType);
		}
	}

	for (unsigned int s = 0; s < config.max_shots; ++s)
	{
		if (shotRec[s].isAvailable)
			continue;
		h = de_net_hash_u32(h, de_net_float_bits(shotRec[s].x));
		h = de_net_hash_u32(h, de_net_float_bits(shotRec[s].y));
	}

	return h;
}

/* Walk the complete Destruct simulation, including collision terrain. Pool sizes and allocations
 * stay fixed for the session; restore re-pins pointers after copying the blob. */
static void DE_StateWalk(Uint8* buf, bool saving)
{
	size_t off = 0;

	#define DE_WALK(mem, bytes)                          \
		do {                                             \
			const size_t n_ = (bytes);                   \
			if (saving)                                  \
				memcpy(buf + off, (mem), n_);            \
			else                                         \
				memcpy((mem), buf + off, n_);            \
			off += n_;                                   \
		} while (false)

	for (unsigned int i = 0; i < MAX_PLAYERS; ++i)
		DE_WALK(destruct_player[i].unit, sizeof(struct destruct_unit_s) * config.max_installations);
	DE_WALK(world.mapWalls, sizeof(struct destruct_wall_s) * config.max_walls);
	DE_WALK(shotRec, sizeof(struct destruct_shot_s) * config.max_shots);
	DE_WALK(exploRec, sizeof(struct destruct_explo_s) * config.max_explosions);
	DE_WALK(destructTempScreen->pixels,
	        (size_t)destructTempScreen->pitch * destructTempScreen->h);

	DE_WALK(&world, sizeof(world));
	DE_WALK(destruct_player, sizeof(destruct_player));
	DE_WALK(&de_endDelay, sizeof(de_endDelay));
	DE_WALK(&destructFirstTime, sizeof(destructFirstTime));

	#undef DE_WALK

	/* The generator holds internal pointers, so it saves and restores itself. */
	if (saving)
		mt_state_save(buf + off);
	else
		mt_state_restore(buf + off);
}

static size_t DE_StateSize(void)
{
	return sizeof(struct destruct_unit_s) * config.max_installations * MAX_PLAYERS
	     + sizeof(struct destruct_wall_s) * config.max_walls
	     + sizeof(struct destruct_shot_s) * config.max_shots
	     + sizeof(struct destruct_explo_s) * config.max_explosions
	     + (size_t)destructTempScreen->pitch * destructTempScreen->h
	     + sizeof(world)
	     + sizeof(destruct_player)
	     + sizeof(de_endDelay)
	     + sizeof(destructFirstTime)
	     + mt_state_size();
}

static void DE_StateSave(void* dst)
{
	DE_StateWalk((Uint8*)dst, true);
}

static void DE_StateRestore(const void* src)
{
	struct destruct_unit_s* units[MAX_PLAYERS];
	for (unsigned int i = 0; i < MAX_PLAYERS; ++i)
		units[i] = destruct_player[i].unit;
	struct destruct_wall_s* const walls = world.mapWalls;
	SDL_Surface* const screen = world.VGAScreen;

	DE_StateWalk((Uint8*)src, false);   /* the restoring walk only reads through buf */

	for (unsigned int i = 0; i < MAX_PLAYERS; ++i)
		destruct_player[i].unit = units[i];
	world.mapWalls = walls;
	world.VGAScreen = screen;
}

/* One lockstep exchange, run at the top of every online tick: sample local input, publish it,
 * block until the peer's packet for the same logical tick is here, and settle what the tick is
 * (simulate / new round / session over).  The action bits it leaves in de_net_local_bits and
 * de_net_peer_bits are applied at the same point of the tick the offline path reads its keys. */
static enum de_state_t DE_NetExchange(void)
{
	service_SDL_events(true);

	const Uint8 actions = DE_NetLocalActions();
	const Uint8 controls = DE_NetLocalControls();

	network_state_prepare();
	packet_state_out[0]->data[4] = actions;
	packet_state_out[0]->data[5] = controls;
	SDLNet_Write32(mt_rand_count,   &packet_state_out[0]->data[NET_STATE_RAND]);
	SDLNet_Write32(DE_NetSimHash(), &packet_state_out[0]->data[NET_STATE_PHASH]);
	network_state_send();

	de_net_have_inputs = false;
	if (!network_state_update())
		return STATE_CONTINUE;   /* the initial delay window: send only, apply nothing */

	/* Our own bytes for the peer packet's logical tick, replayed out of the outbound queue the
	 * way the main game's lockstep replays its ship (see JE_playerMovement). */
	const Uint8 ownActions   = packet_state_out[network_delay]->data[4];
	const Uint8 ownControls  = packet_state_out[network_delay]->data[5];
	const Uint8 peerActions  = packet_state_in[0]->data[4];
	const Uint8 peerControls = packet_state_in[0]->data[5];

	/* Both canaries were written before their tick's inputs applied, so a mismatch is a real
	 * divergence, not skew.  One report per session; play continues (the classic destruct rule
	 * of thumb: a desynced artillery duel is still more fun than a halted one). */
	if (!de_net_desync_noted)
	{
		const Uint32 theirRand = SDLNet_Read32(&packet_state_in[0]->data[NET_STATE_RAND]);
		const Uint32 ourRand   = SDLNet_Read32(&packet_state_out[network_delay]->data[NET_STATE_RAND]);
		const Uint32 theirHash = SDLNet_Read32(&packet_state_in[0]->data[NET_STATE_PHASH]);
		const Uint32 ourHash   = SDLNet_Read32(&packet_state_out[network_delay]->data[NET_STATE_PHASH]);

		if (theirRand != ourRand || theirHash != ourHash)
		{
			de_net_desync_noted = true;

			char detail[192];
			snprintf(detail, sizeof(detail),
			         "Destruct round %u, player %u, delay %d\n"
			         "  rand draws : local %lu  remote %lu\n"
			         "  sim hash   : local %08lx  remote %08lx",
			         de_net_round, thisPlayerNum, network_delay,
			         (unsigned long)ourRand, (unsigned long)theirRand,
			         (unsigned long)ourHash, (unsigned long)theirHash);
			crashlog_netlog_line("DESTRUCT DESYNC", detail);
			network_diag_note_desync(-1);
		}
	}

	const Uint8 bothControls = ownControls | peerControls;

	// Both machines consume the same bit at the same logical tick, so both leave together.
	if (bothControls & DRB_CTRL_QUIT)
		return STATE_INIT;

	if (bothControls & DRB_CTRL_NEWMAP)
		return STATE_RELOAD;

	de_net_local_bits = ownActions;
	de_net_peer_bits = peerActions;
	de_net_have_inputs = true;

	return STATE_CONTINUE;
}

// The delayed input pair, applied where the offline path samples its keys.  Inside the delay
// window there is nothing to apply and DE_ResetActions has already cleared every move.
static void DE_NetApplyMoves(void)
{
	if (!de_net_have_inputs)
		return;

	bool* localMoves = destruct_player[de_net_local_side].moves.actions;
	bool* peerMoves = destruct_player[1 - de_net_local_side].moves.actions;

	for (int i = 0; i < MAX_MOVE; ++i)
	{
		localMoves[i] = (de_net_local_bits & (1 << i)) != 0;
		peerMoves[i] = (de_net_peer_bits & (1 << i)) != 0;
	}
}

/* Scripted input for the snapshot self-test, which has no keyboard and no peer.  A plain LCG
 * rather than mt_rand: the script must not draw on the simulation's own generator.  Both sides
 * hold directions and fire freely and cycle units and weapons occasionally, so the battle keeps
 * shots, craters and unit changes coming for the replay to disagree about. */
static Uint8 DE_SelfTestActions(void)
{
	static Uint32 script = 0x9E3779B9u;

	script = script * 1103515245u + 12345u;
	const Uint32 r = script >> 8;

	Uint8 bits = (Uint8)(r & DE_ROLLBACK_HELD_ACTIONS);
	if ((r & 0x1E00) == 0)
		bits |= 1 << MOVE_CHANGE;
	if ((r & 0x3C000) == 0)
		bits |= 1 << MOVE_CYUP;
	if ((r & 0x78000) == 0)
		bits |= 1 << MOVE_CYDN;
	return bits;
}

/* The rollback counterpart of DE_NetApplyMoves, at the same point of the tick.  A live pass reads
 * the keyboard and records what it read; a re-simulation replays that record instead, so the
 * frames behind a correction consume exactly the input they consumed the first time. */
static void DE_RollbackApplyMoves(void)
{
	if (!drb_resim())
	{
		if (drb_selftest_active())
		{
			drb_selftest_feed(DE_SelfTestActions(), DE_SelfTestActions());
		}
		else
		{
			service_SDL_events(true);
			drb_record_local(DE_NetLocalActions(), DE_NetLocalControls());
		}
	}

	Uint8 localBits, peerBits;
	drb_frame_actions(&localBits, &peerBits);

	bool* localMoves = destruct_player[de_net_local_side].moves.actions;
	bool* peerMoves = destruct_player[1 - de_net_local_side].moves.actions;

	for (int i = 0; i < MAX_MOVE; ++i)
	{
		localMoves[i] = (localBits & (1 << i)) != 0;
		peerMoves[i] = (peerBits & (1 << i)) != 0;
	}
}

#endif  /* WITH_NETWORK */

/* Returns the state requested after one complete Destruct tick. */
static enum de_state_t DE_RunTick(void)
{
	setDelay(1);

	// Aim, power, fire, and the two cyclers are more than a pad's face buttons carry, so
	// the touch ports get a Destruct-specific set (see DE_TouchActions).
	touch_ui_set_layout(TOUCH_LAYOUT_DESTRUCT);

#ifdef WITH_NETWORK
	// The lockstep exchange leads the tick so its verdicts (leave, new round) settle
	// before any sim state moves, the explosion-glow fade below included.  Rollback settles
	// the same verdicts at the bottom instead, once both machines' input for the frame is in.
	if (de_net && !de_net_rollback)
	{
		const enum de_state_t netVerdict = DE_NetExchange();
		if (netVerdict != STATE_CONTINUE)
			return netVerdict;
	}

de_sim_pass:
	// Snapshot before anything this frame moves, so a correction can restore the frame whole.
	drb_frame_begin();
#endif

	memset(soundQueue, 0, sizeof(soundQueue));

	/* A silent re-simulation pass corrects state that is already on screen, so it does the whole
	 * tick with the present, the sound and the frame delay switched off.  Only the pass that
	 * catches the timeline up reaches the screen.  The self-test presents nothing at all: it has
	 * no viewer and runs as fast as the machine can simulate. */
	const bool de_present = !drb_resim_silent() && !drb_selftest_active();

	JE_tempScreenChecking();

	/* The smooth present kicks in once we're past the first (fade-in) tick, when the
	 * user has Smooth Motion on and supersampling is running.  When it does, capture
	 * the clean terrain before this tick draws units and shots, allowing the
	 * interpolated frames can be rebuilt from a static background. */
	const int de_ss = effective_supersample();
	const bool smooth = de_present && smoothMotion && de_ss > 1 && !destructFirstTime
	                    && DE_ensureSmoothBuffers(de_ss);
	if (smooth)
		DE_ExpandBackgroundHi(de_ss);

	DE_ResetActions();
	DE_RunTickCycleDeadUnits();

	DE_RunTickGravity();
	DE_RunTickAnimate();
	DE_RunTickDrawWalls();
	DE_RunTickExplosions();
	DE_RunTickShots();
	DE_RunTickAI();
	DE_RunTickDrawCrosshairs();
	DE_RunTickDrawHUD();
	if (de_present && !smooth)
		JE_showVGA();   /* smooth path presents in DE_SmoothPresent below */

	if (destructFirstTime)
	{
		/* The fade belongs to the first live pass only.  A correction that reaches back to frame 1
		 * restores the flag with everything else, and fading a palette that is already up would
		 * stall the battle for the fade's own 25 ticks. */
		if (de_present && !drb_resim())
			fade_palette(colors, 25, 0, 255);
		destructFirstTime = false;
		de_endDelay = 0;
	}

	bool de_round_over = false;

#ifdef WITH_NETWORK
	if (de_net_rollback || drb_selftest_active())
		DE_RollbackApplyMoves();
	else if (de_net)
		DE_NetApplyMoves();
	else
#endif
	DE_RunTickGetInput();
	DE_ProcessInput();

	if (de_endDelay > 0)
	{
		if (--de_endDelay == 0)
			de_round_over = true;
	}
	else if (DE_RunTickCheckEndgame() == true)
	{
		de_endDelay = 80;
	}

	/* Under rollback the verdict is held for the driver below, which only lets the round end once
	 * both machines have confirmed the frames behind it.  Every other path has nothing to confirm
	 * and leaves on the spot, ahead of this tick's sounds, the way it always has. */
	if (de_round_over && !drb_active())
		return STATE_RELOAD;

	// A re-simulation pass would replay sounds the live pass it corrects already played.
	if (!drb_resim())
		DE_RunTickPlaySounds();

	/* Offline-only shortcuts. Online control bits own quit, and blocking or local
	 * toggles here would stall or split the simulation. */
#ifdef WITH_NETWORK
	if (!de_net)
#endif
	{
	if (keysactive[SDL_SCANCODE_F10])
	{
		destruct_player[PLAYER_LEFT].is_cpu = !destruct_player[PLAYER_LEFT].is_cpu;
		keysactive[SDL_SCANCODE_F10] = false;
	}
	if (keysactive[SDL_SCANCODE_F11])
	{
		destruct_player[PLAYER_RIGHT].is_cpu = !destruct_player[PLAYER_RIGHT].is_cpu;
		keysactive[SDL_SCANCODE_F11] = false;
	}
	if (keysactive[SDL_SCANCODE_P])
	{
		JE_pauseScreen();
		keysactive[lastkey_scan] = false;
	}

	if (keysactive[SDL_SCANCODE_F1])
	{
		JE_helpScreen();
		keysactive[lastkey_scan] = false;
	}
	}

#ifdef WITH_NETWORK
	/* The self-test replays every frame in place of the driver, which needs a peer. */
	if (drb_selftest_active())
	{
		if (drb_selftest_tick())
			goto de_sim_pass;
	}
	/* Rollback driver: publish this frame's input, take in the peer's, and either correct the
	 * timeline or let the frame stand.  It runs before the delay below so a correction is
	 * replayed inside the tick's own slack rather than a frame late. */
	else if (de_net_rollback)
	{
		switch (drb_driver(de_round_over))
		{
		case DRB_STEP_RESIM:
			goto de_sim_pass;
		case DRB_STEP_QUIT:
			return STATE_INIT;
		case DRB_STEP_NEWMAP:
			return STATE_RELOAD;
		case DRB_STEP_PRESENT:
			break;
		}
	}
#endif

	/* Present the tick.  In smooth mode this loop spans the tick period, drawing
	 * interpolated supersampled frames; otherwise just wait out the period. */
	if (smooth)
		DE_SmoothPresent(de_ss);
	else if (de_present)
		wait_delay();

#ifdef WITH_NETWORK
	// Online, leaving and reloading are netcode verdicts, settled by the lockstep exchange up top
	// or by the rollback driver above; a local key acting here would end one machine's round and
	// not the other's.
	if (de_net)
		return STATE_CONTINUE;
#endif

	if (keysactive[SDL_SCANCODE_ESCAPE])
	{
		keysactive[SDL_SCANCODE_ESCAPE] = false;
		return STATE_INIT; /* STATE_INIT drops us to the mode select */
	}

	if (keysactive[SDL_SCANCODE_BACKSPACE])
	{
		keysactive[SDL_SCANCODE_BACKSPACE] = false;
		return STATE_RELOAD; /* STATE_RELOAD creates a new map */
	}

	return STATE_CONTINUE;
}

static void DE_RunTickCycleDeadUnits(void)
{
	unsigned int i;
	struct destruct_unit_s* unit;

	/* Select the next living unit with a valid weapon. */
	for (i = 0; i < MAX_PLAYERS; i++)
	{
		if (destruct_player[i].unitsRemaining == 0)
			continue;

		unit = &(destruct_player[i].unit[destruct_player[i].unitSelected]);
		while (DE_isValidUnit(unit) == false ||
			unit->shotType == SHOT_INVALID)
		{
			destruct_player[i].unitSelected++;
			unit++;
			if (destruct_player[i].unitSelected >= config.max_installations)
			{
				destruct_player[i].unitSelected = 0;
				unit = destruct_player[i].unit;
			}
		}
	}
}

static void DE_RunTickGravity(void)
{
	unsigned int i, j;
	struct destruct_unit_s* unit;

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		unit = destruct_player[i].unit;
		for (j = 0; j < config.max_installations; j++, unit++)
		{
			if (DE_isValidUnit(unit) == false) /* invalid unit */
				continue;

			/* Remember the pre-tick position so the smooth present can interpolate
			 * from here to wherever this tick's gravity (and later input) leaves it. */
			unit->prev_x = unit->unitX;
			unit->prev_y = unit->unitY;

			switch (unit->unitType)
			{
			case UNIT_SATELLITE: /* satellites don't fall down */
				break;

			case UNIT_HELI:
			case UNIT_JUMPER:
				if (unit->isYInAir == true) /* unit is falling down, at least in theory */
				{
					DE_GravityFlyUnit(unit);
					break;
				}
				/* else treat as a normal unit */
				/* fall through */
			default:
				DE_GravityLowerUnit(unit);
			}

			/* Draw the unit. */
			DE_GravityDrawUnit(i, unit);
		}
	}
}

static unsigned int DE_unitAnimIndex(enum de_player_t team, const struct destruct_unit_s* unit)
{
	unsigned int anim_index = GraphicBase[team][unit->unitType] + unit->ani_frame;
	if (unit->unitType == UNIT_HELI)
	{
		/* Adjust animation index if we are traveling right or left. */
		if (unit->lastMove < -2)
			anim_index += 5;
		else if (unit->lastMove > 2)
			anim_index += 10;
	}
	else /* This handles our cannons and the like */
	{
		anim_index += floorf(unit->angle * 9.99f / M_PI);
	}
	return anim_index;
}

static void DE_GravityDrawUnit(enum de_player_t team, struct destruct_unit_s* unit)
{
	blit_sprite2(VGAScreen, unit->unitX, roundf(unit->unitY) - 13, destructSpriteSheet, DE_unitAnimIndex(team, unit));
}

static void DE_GravityLowerUnit(struct destruct_unit_s* unit)
{
	/* Ground units fall at constant speed. Helicopters retain the slower descent
	 * and downward velocity used by the Tyrian 2000 behavior. */
	if (unit->unitY < 199)  /* checking takes time, don't check if it's at the bottom */
	{
		if (JE_stabilityCheck(unit->unitX, roundf(unit->unitY)))
		{
			switch (unit->unitType)
			{
			case UNIT_HELI:
				unit->unitYMov = 1.5f;
				unit->unitY += 0.2f;
				break;

			default:
				unit->unitY += 1;
			}

			if (unit->unitY > 199) /* could be possible */
				unit->unitY = 199;
		}
	}
}

static void DE_GravityFlyUnit(struct destruct_unit_s* unit)
{
	if (unit->unitY + unit->unitYMov > 199) /* would hit bottom of screen */
	{
		unit->unitY = 199;
		unit->unitYMov = 0;
		unit->isYInAir = false;
		return;
	}

	/* move the unit and alter acceleration */
	unit->unitY += unit->unitYMov;
	if (unit->unitY < 24) /* This stops units from going above the screen */
	{
		unit->unitYMov = 0;
		unit->unitY = 24;
	}

	if (unit->unitType == UNIT_HELI) /* helicopters fall more slowly */
		unit->unitYMov += 0.0001f;
	else
		unit->unitYMov += 0.03f;

	if (!JE_stabilityCheck(unit->unitX, roundf(unit->unitY)))
	{
		unit->unitYMov = 0;
		unit->isYInAir = false;
	}
}

static void DE_RunTickAnimate(void)
{
	unsigned int p, u;
	struct destruct_unit_s* ptr;

	for (p = 0; p < MAX_PLAYERS; ++p)
	{
		ptr = destruct_player[p].unit;
		for (u = 0; u < config.max_installations; ++u, ++ptr)
		{
			/* Don't mess with any unit that is unallocated
			 * or doesn't animate and is set to frame 0 */
			if (DE_isValidUnit(ptr) == false)
				continue;
			if (systemAni[ptr->unitType] == false && ptr->ani_frame == 0)
				continue;

			if (++(ptr->ani_frame) > 3)
				ptr->ani_frame = 0;
		}
	}
}

static void DE_RunTickDrawWalls(void)
{
	unsigned int i;

	for (i = 0; i < config.max_walls; i++)
		if (world.mapWalls[i].wallExist)
			blit_sprite2(VGAScreen, world.mapWalls[i].wallX, world.mapWalls[i].wallY, destructSpriteSheet, 42);
}

static void DE_RunTickExplosions(void)
{
	unsigned int i, j;
	int tempPosX, tempPosY;
	float tempRadian;

	/* Run through all open explosions.  They are not sorted in any way */
	for (i = 0; i < config.max_explosions; i++)
	{
		if (exploRec[i].isAvailable == true)
			continue;  /* Nothing to do */

		for (j = 0; j < exploRec[i].explofill; j++)
		{
			/* An explosion is comprised of multiple 'flares' that fan out.
			   Calculate where this 'flare' will end up */
			tempRadian = mt_rand_lt1() * (2 * M_PI);
			tempPosY = exploRec[i].y + roundf(sim_cosf(tempRadian) * mt_rand_lt1() * exploRec[i].explowidth);
			tempPosX = exploRec[i].x + roundf(sim_sinf(tempRadian) * mt_rand_lt1() * exploRec[i].explowidth);

			/* Preserve explosion wrapping without out-of-bounds access. */

			while (tempPosX < 0)
				tempPosX += vga_width;
			while (tempPosX >= vga_width)
				tempPosX -= vga_width;

			/* Normal glow may enter the open sky between HUD boxes because it erases itself. Dirt
			 * remains below the classic ceiling; persistent terrain is never repainted up there. */
			{
				const bool inSky = tempPosX >= HUD_GAP_LEFT && tempPosX < HUD_FRAME_RIGHT_X
				                && exploRec[i].exploType == EXPL_NORMAL;
				if (tempPosY >= 200 || tempPosY <= (inSky ? 0 : 15))
					continue;
			}

			switch (exploRec[i].exploType)
			{
			case EXPL_DIRT:
				((Uint8*)destructTempScreen->pixels)[tempPosX + tempPosY * destructTempScreen->pitch] = PIXEL_DIRT;
				break;

			case EXPL_NORMAL:
				JE_superPixel(tempPosX, tempPosY);
				DE_TestExplosionCollision(tempPosX, tempPosY);
				break;

			default:
				assert(false);
				break;
			}
		}

		/* Widen the explosion and delete it if necessary. */
		exploRec[i].explowidth++;
		if (exploRec[i].explowidth == exploRec[i].explomax)
		{
			exploRec[i].isAvailable = true;
		}
	}
}

static void DE_TestExplosionCollision(unsigned int PosX, unsigned int PosY)
{
	unsigned int i, j;
	struct destruct_unit_s* unit;

	for (i = PLAYER_LEFT; i < MAX_PLAYERS; i++)
	{
		unit = destruct_player[i].unit;
		for (j = 0; j < config.max_installations; j++, unit++)
		{
			if (DE_isValidUnit(unit) == true &&
				PosX > unit->unitX && PosX < unit->unitX + 11 &&
				PosY < unit->unitY && PosY > unit->unitY - 11)
			{
				unit->health--;
				if (unit->health <= 0)
					DE_DestroyUnit(i, unit);
			}
		}
	}
}

static void DE_DestroyUnit(enum de_player_t playerID, struct destruct_unit_s* unit)
{
	// Only helicopters use the small-shot explosion.
	JE_makeExplosion(unit->unitX + 5, roundf(unit->unitY) - 5, (unit->unitType == UNIT_HELI) ? SHOT_SMALL : SHOT_INVALID);

	if (unit->unitType != UNIT_SATELLITE)
	{
		destruct_player[playerID].unitsRemaining--;
		destruct_player[((playerID == PLAYER_LEFT) ? PLAYER_RIGHT : PLAYER_LEFT)].score++;
	}
}

static void DE_RunTickShots(void)
{
	unsigned int i, j, k;
	unsigned int tempTrails;
	unsigned int tempPosX, tempPosY;
	struct destruct_unit_s* unit;

	for (i = 0; i < config.max_shots; i++)
	{
		if (shotRec[i].isAvailable == true)
			continue;  /* Nothing to do */

		/* Remember the pre-move position so the smooth present can interpolate. */
		shotRec[i].prev_x = shotRec[i].x;
		shotRec[i].prev_y = shotRec[i].y;

		shotRec[i].x += shotRec[i].xmov;
		shotRec[i].y += shotRec[i].ymov;

		/* If the shot can bounce off the map, bounce it */
		if (shotBounce[shotRec[i].shottype])
		{
			/* The ceiling follows the sky window: between the HUD boxes a bouncing shot may
			 * climb to the top of the screen instead of rebounding off thin air at y=14 in
			 * the middle of the open window.  Over the boxes the classic ceiling stands (a
			 * shot that drifts out of the window while high self-corrects on the flip). */
			const float ceiling = (shotRec[i].x >= HUD_GAP_LEFT && shotRec[i].x < HUD_FRAME_RIGHT_X)
			                    ? 1.0f : 14.0f;
			if (shotRec[i].y > 199 || shotRec[i].y < ceiling)
			{
				shotRec[i].y -= shotRec[i].ymov;
				shotRec[i].ymov = -shotRec[i].ymov;
			}
			if (shotRec[i].x < 1 || shotRec[i].x > vga_width - 2)
			{
				shotRec[i].x -= shotRec[i].xmov;
				shotRec[i].xmov = -shotRec[i].xmov;
			}
		}
		else /* If it cannot, apply normal physics */
		{
			shotRec[i].ymov += 0.05f; /* add gravity */

			if (shotRec[i].y > 199) /* We hit the floor */
			{
				shotRec[i].y -= shotRec[i].ymov;
				shotRec[i].ymov = -shotRec[i].ymov * 0.8f; /* bounce at reduced velocity */

				/* Don't allow a bouncing shot to bounce straight up and down */
				if (shotRec[i].xmov == 0)
					shotRec[i].xmov += mt_rand_lt1() - 0.5f;
			}
		}

		/* Shot has gone out of bounds. Eliminate it. */
		if (shotRec[i].x > vga_width - 2 || shotRec[i].x < 1)
		{
			shotRec[i].isAvailable = true;
			continue;
		}

		/* Draw the shot (and its trail) first; even above the map, so it shows
		 * in the playfield gap between the HUD boxes.  Only while it's actually
		 * on-screen; collisions are gated separately below. */
		tempPosX = roundf(shotRec[i].x);
		tempTrails = (shotColor[shotRec[i].shottype] << 4) - 3;

		if (shotRec[i].y >= 0 && shotRec[i].y < vga_height)
		{
			tempPosY = roundf(shotRec[i].y);
			JE_pixCool(tempPosX, tempPosY, tempTrails);

			/*Draw the shot trail (if applicable) */
			switch (shotTrail[shotRec[i].shottype])
			{
			case TRAILS_NONE:
				break;
			case TRAILS_NORMAL:
				DE_DrawTrails(&(shotRec[i]), 2, 4, tempTrails - 3);
				break;
			case TRAILS_FULL:
				DE_DrawTrails(&(shotRec[i]), 4, 3, tempTrails - 1);
				break;
			}
		}

		/* Skip collision checks above the map -- except in the sky window between the HUD
		 * boxes, where the playfield runs to the top of the screen and whatever stands up
		 * there (a wall top, a ring's dirt at rows 12-14) has to be solid, not a ghost the
		 * shots pass through.  Over the HUD boxes the classic ceiling stands. */
		{
			const bool inSky = tempPosX >= HUD_GAP_LEFT && tempPosX < HUD_FRAME_RIGHT_X;
			if (shotRec[i].y <= (inSky ? 0 : 14))
				continue;
		}

		tempPosY = roundf(shotRec[i].y);

		/*Check building hits*/
		for (j = 0; j < MAX_PLAYERS; j++)
		{
			unit = destruct_player[j].unit;
			for (k = 0; k < config.max_installations; k++, unit++)
			{
				if (DE_isValidUnit(unit) == false)
					continue;

				if (tempPosX > unit->unitX && tempPosX < unit->unitX + 11 &&
					tempPosY < unit->unitY && tempPosY > unit->unitY - 13)
				{
					shotRec[i].isAvailable = true;
					JE_makeExplosion(tempPosX, tempPosY, shotRec[i].shottype);
				}
			}
		}

		/* Bounce off of or destroy walls */
		for (j = 0; j < config.max_walls; j++)
		{
			if (world.mapWalls[j].wallExist == true &&
				tempPosX >= world.mapWalls[j].wallX && tempPosX <= world.mapWalls[j].wallX + 11 &&
				tempPosY >= world.mapWalls[j].wallY && tempPosY <= world.mapWalls[j].wallY + 14)
			{
				if (demolish[shotRec[i].shottype])
				{
					/* Blow up the wall and remove the shot. */
					world.mapWalls[j].wallExist = false;
					shotRec[i].isAvailable = true;
					JE_makeExplosion(tempPosX, tempPosY, shotRec[i].shottype);
					continue;
				}
				else
				{
					/* Otherwise, bounce. */
					if (shotRec[i].x - shotRec[i].xmov < world.mapWalls[j].wallX ||
						shotRec[i].x - shotRec[i].xmov > world.mapWalls[j].wallX + 11)
					{
						shotRec[i].xmov = -shotRec[i].xmov;
					}
					if (shotRec[i].y - shotRec[i].ymov < world.mapWalls[j].wallY ||
						shotRec[i].y - shotRec[i].ymov > world.mapWalls[j].wallY + 14)
					{
						if (shotRec[i].ymov < 0)
							shotRec[i].ymov = -shotRec[i].ymov;
						else
							shotRec[i].ymov = -shotRec[i].ymov * 0.8f;
					}

					tempPosX = roundf(shotRec[i].x);
					tempPosY = roundf(shotRec[i].y);
				}
			}
		}

		/* Our last collision check, at least for now.  We hit dirt. */
		if ((((Uint8*)destructTempScreen->pixels)[tempPosX + tempPosY * destructTempScreen->pitch]) == PIXEL_DIRT)
		{
			shotRec[i].isAvailable = true;
			JE_makeExplosion(tempPosX, tempPosY, shotRec[i].shottype);
			continue;
		}
	}
}

static void DE_DrawTrails(struct destruct_shot_s* shot, unsigned int count, unsigned int decay, unsigned int startColor)
{
	int i;

	for (i = count - 1; i >= 0; i--) /* Reverse order determines trail layering. */
	{
		if (shot->trailc[i] > 0 && shot->traily[i] > 0) /* exists and on-screen (HUD boxes are repainted over) -> draw it */
		{
			JE_pixCool(shot->trailx[i], shot->traily[i], shot->trailc[i]);
		}

		if (i == 0) /* The first trail we create. */
		{
			shot->trailx[i] = roundf(shot->x);
			shot->traily[i] = roundf(shot->y);
			shot->trailc[i] = startColor;
		}
		else /* The newer trails decay into the older trails.*/
		{
			shot->trailx[i] = shot->trailx[i - 1];
			shot->traily[i] = shot->traily[i - 1];
			if (shot->trailc[i - 1] > 0)
			{
				shot->trailc[i] = shot->trailc[i - 1] - decay;
			}
		}
	}
}

static void DE_RunTickAI(void)
{
	unsigned int i, j;
	struct destruct_player_s* ptrPlayer, * ptrTarget;
	struct destruct_unit_s* ptrUnit, * ptrCurUnit;

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		ptrPlayer = &(destruct_player[i]);
		if (ptrPlayer->is_cpu == false)
			continue;

		/* Each CPU targets the next player slot. */
		j = i + 1;
		if (j >= MAX_PLAYERS)
			j = 0;

		ptrTarget = &(destruct_player[j]);
		ptrCurUnit = &(ptrPlayer->unit[ptrPlayer->unitSelected]);

		if (ptrPlayer->aiMemory.c_noDown > 0)
			ptrPlayer->aiMemory.c_noDown--;

		/* Until all structs are properly divvied up this must only apply to player1 */
		if (mt_rand() % 100 > 80)
		{
			ptrPlayer->aiMemory.c_Angle += (mt_rand() % 3) - 1;

			if (ptrPlayer->aiMemory.c_Angle > 1)
				ptrPlayer->aiMemory.c_Angle = 1;
			else
				if (ptrPlayer->aiMemory.c_Angle < -1)
					ptrPlayer->aiMemory.c_Angle = -1;
		}
		if (mt_rand() % 100 > 90)
		{
			if (ptrPlayer->aiMemory.c_Angle > 0 && ptrCurUnit->angle > (M_PI_2)-(M_PI / 9))
				ptrPlayer->aiMemory.c_Angle = 0;
			else
				if (ptrPlayer->aiMemory.c_Angle < 0 && ptrCurUnit->angle < M_PI / 8)
					ptrPlayer->aiMemory.c_Angle = 0;
		}

		if (mt_rand() % 100 > 93)
		{
			ptrPlayer->aiMemory.c_Power += (mt_rand() % 3) - 1;

			if (ptrPlayer->aiMemory.c_Power > 1)
				ptrPlayer->aiMemory.c_Power = 1;
			else
				if (ptrPlayer->aiMemory.c_Power < -1)
					ptrPlayer->aiMemory.c_Power = -1;
		}
		if (mt_rand() % 100 > 90)
		{
			if (ptrPlayer->aiMemory.c_Power > 0 && ptrCurUnit->power > 4)
				ptrPlayer->aiMemory.c_Power = 0;
			else
				if (ptrPlayer->aiMemory.c_Power < 0 && ptrCurUnit->power < 3)
					ptrPlayer->aiMemory.c_Power = 0;
				else
					if (ptrCurUnit->power < 2)
						ptrPlayer->aiMemory.c_Power = 1;
		}

		// prefer helicopter
		ptrUnit = ptrPlayer->unit;
		for (j = 0; j < config.max_installations; j++, ptrUnit++)
		{
			if (DE_isValidUnit(ptrUnit) && ptrUnit->unitType == UNIT_HELI)
			{
				ptrPlayer->unitSelected = j;
				break;
			}
		}

		if (ptrCurUnit->unitType == UNIT_HELI)
		{
			if (ptrCurUnit->isYInAir == false)
			{
				ptrPlayer->aiMemory.c_Power = 1;
			}
			if (mt_rand() % ptrCurUnit->unitX > 100)
			{
				ptrPlayer->aiMemory.c_Power = 1;
			}
			if (mt_rand() % (vga_width - 80) > ptrCurUnit->unitX)
			{
				ptrPlayer->moves.actions[MOVE_RIGHT] = true;
			}
			else if ((mt_rand() % 20) + (vga_width - 20) < ptrCurUnit->unitX)
			{
				ptrPlayer->moves.actions[MOVE_LEFT] = true;
			}
			else if (mt_rand() % 30 == 1)
			{
				ptrPlayer->aiMemory.c_Angle = (mt_rand() % 3) - 1;
			}
			if (ptrCurUnit->unitX > vga_width - 25 && ptrCurUnit->lastMove > 1)
			{
				ptrPlayer->moves.actions[MOVE_LEFT] = true;
				ptrPlayer->moves.actions[MOVE_RIGHT] = false;
			}
			if (ptrCurUnit->unitType != UNIT_HELI || ptrCurUnit->lastMove > 3 || (ptrCurUnit->unitX > vga_width / 2 && ptrCurUnit->lastMove > -3))
			{
				if (mt_rand() % (int)roundf(ptrCurUnit->unitY) < 150 && ptrCurUnit->unitYMov < 0.01f && (ptrCurUnit->unitX < vga_width / 2 || ptrCurUnit->lastMove < 2))
					ptrPlayer->moves.actions[MOVE_FIRE] = true;
				ptrPlayer->aiMemory.c_noDown = (5 - abs(ptrCurUnit->lastMove)) * (5 - abs(ptrCurUnit->lastMove)) + 3;
				ptrPlayer->aiMemory.c_Power = 1;
			}
			else
			{
				ptrPlayer->moves.actions[MOVE_FIRE] = false;
			}

			ptrUnit = ptrTarget->unit;
			for (j = 0; j < config.max_installations; j++, ptrUnit++)
			{
				if (abs((int)ptrUnit->unitX - (int)ptrCurUnit->unitX) < 8)
				{
					/* Helicopters hover over their targets. */
					if (ptrUnit->unitType == UNIT_SATELLITE)
					{
						ptrPlayer->moves.actions[MOVE_FIRE] = false;
					}
					else
					{
						ptrPlayer->moves.actions[MOVE_LEFT] = false;
						ptrPlayer->moves.actions[MOVE_RIGHT] = false;
						if (ptrCurUnit->lastMove < -1)
							ptrCurUnit->lastMove++;
						else if (ptrCurUnit->lastMove > 1)
							ptrCurUnit->lastMove--;
					}
				}
			}
		}
		else
		{
			ptrPlayer->moves.actions[MOVE_FIRE] = 1;
		}

		if (mt_rand() % 200 > 198)
		{
			ptrPlayer->moves.actions[MOVE_CHANGE] = true;
			ptrPlayer->aiMemory.c_Angle = 0;
			ptrPlayer->aiMemory.c_Power = 0;
			ptrPlayer->aiMemory.c_Fire = 0;
		}

		if (mt_rand() % 100 > 98 || ptrCurUnit->shotType == SHOT_TRACER)
		{
			ptrPlayer->moves.actions[MOVE_CYDN] = true;
		}
		if (ptrPlayer->aiMemory.c_Angle > 0)
		{
			ptrPlayer->moves.actions[MOVE_LEFT] = true;
		}
		if (ptrPlayer->aiMemory.c_Angle < 0)
		{
			ptrPlayer->moves.actions[MOVE_RIGHT] = true;
		}
		if (ptrPlayer->aiMemory.c_Power > 0)
		{
			ptrPlayer->moves.actions[MOVE_UP] = true;
		}
		if (ptrPlayer->aiMemory.c_Power < 0 && ptrPlayer->aiMemory.c_noDown == 0)
		{
			ptrPlayer->moves.actions[MOVE_DOWN] = true;
		}
		if (ptrPlayer->aiMemory.c_Fire > 0)
		{
			ptrPlayer->moves.actions[MOVE_FIRE] = true;
		}

		if (ptrCurUnit->unitYMov < -0.1f && ptrCurUnit->unitType == UNIT_HELI)
		{
			ptrPlayer->moves.actions[MOVE_FIRE] = false;
		}

		/* Laser and airborne units cancel the AI power hold. */
		if (ptrCurUnit->unitType == UNIT_LASER || ptrCurUnit->isYInAir == true)
			ptrPlayer->aiMemory.c_Power = 0;
	}
}

static void DE_RunTickDrawCrosshairs(void)
{
	unsigned int i;
	int tempPosX, tempPosY;
	int direction;
	struct destruct_unit_s* curUnit;

	/* Draw the crosshairs.  Most vehicles aim left or right.  Helis can aim
	 * either way and this must be accounted for.
	 */
	for (i = 0; i < MAX_PLAYERS; i++)
	{
		direction = (i == PLAYER_LEFT) ? -1 : 1;
		curUnit = &(destruct_player[i].unit[destruct_player[i].unitSelected]);

		if (curUnit->unitType == UNIT_HELI)
		{
			tempPosX = curUnit->unitX + roundf(0.1f * curUnit->lastMove * curUnit->lastMove * curUnit->lastMove) + 5;
			tempPosY = roundf(curUnit->unitY) + 1;
		}
		else
		{
			tempPosX = roundf(curUnit->unitX + 6 - cosf(curUnit->angle) * (curUnit->power * 8 + 7) * direction);
			tempPosY = roundf(curUnit->unitY - 7 - sinf(curUnit->angle) * (curUnit->power * 8 + 7));
		}

		/* Draw it.  Clip away from the HUD though. */
		if (tempPosY > 9)
		{
			if (tempPosY > 11)
			{
				if (tempPosY > 13)
				{
					/* Top pixel */
					JE_pix(VGAScreen, tempPosX, tempPosY - 2, 3);
				}
				/* Middle three pixels */
				JE_pix(VGAScreen, tempPosX + 3, tempPosY, 3);
				JE_pix(VGAScreen, tempPosX, tempPosY, 14);
				JE_pix(VGAScreen, tempPosX - 3, tempPosY, 3);
			}
			/* Bottom pixel */
			JE_pix(VGAScreen, tempPosX, tempPosY + 2, 3);
		}
	}
}

static void DE_RunTickDrawHUD(void)
{
	unsigned int i;
	int startX;
	char tempstr[16]; /* Max size needed: 16 assuming 10 digit int max. */
	struct destruct_unit_s* curUnit;

	/* Repaint the clean HUD backdrop under the two boxes first, so units or walls
	 * that poke into the top rows can't scribble on the HUD art.  Only the box
	 * columns are repainted; the gap between them (HUD_GAP_LEFT..HUD_FRAME_RIGHT_X)
	 * is left as live playfield, restored each tick by JE_tempScreenChecking. */
	for (unsigned int y = 0; y < HUD_ROWS; ++y)
	{
		Uint8* dst = (Uint8*)VGAScreen->pixels + y * VGAScreen->pitch;
		const Uint8* src = hudBackdrop + y * vga_width;
		memcpy(dst, src, HUD_GAP_LEFT);                                                            /* left box columns  */
		memcpy(dst + HUD_FRAME_RIGHT_X, src + HUD_FRAME_RIGHT_X, vga_width - HUD_FRAME_RIGHT_X);    /* right box columns */
	}

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		curUnit = &(destruct_player[i].unit[destruct_player[i].unitSelected]);
		/* Anchor each box 1px inside its flush-mounted frame (see HUD defines). */
		startX = ((i == PLAYER_LEFT) ? HUD_FRAME_LEFT_X : HUD_FRAME_RIGHT_X) - HUD_BOX_OFFSET;

		fill_rectangle_xy(VGAScreen, startX + 5, 3, startX + 14, 8, 241);
		JE_rectangle(VGAScreen, startX + 4, 2, startX + 15, 9, 242);
		JE_rectangle(VGAScreen, startX + 3, 1, startX + 16, 10, 240);
		fill_rectangle_xy(VGAScreen, startX + 18, 3, startX + 140, 8, 241);
		JE_rectangle(VGAScreen, startX + 17, 2, startX + 143, 9, 242);
		JE_rectangle(VGAScreen, startX + 16, 1, startX + 144, 10, 240);

		blit_sprite2(VGAScreen, startX + 4, 0, destructSpriteSheet, 191 + curUnit->shotType);

		JE_outText(VGAScreen, startX + 20, 3, weaponNames[curUnit->shotType], 15, 2);
		sprintf(tempstr, "dmg~%d~", curUnit->health);
		JE_outText(VGAScreen, startX + 75, 3, tempstr, 15, 0);
		sprintf(tempstr, "pts~%u~", destruct_player[i].score);
		JE_outText(VGAScreen, startX + 110, 3, tempstr, 15, 0);
	}
}

static void DE_RunTickGetInput(void)
{
	unsigned int player_index, key_index, slot_index;
	SDL_Scancode key;

	/* Key and action arrays share indices, including alternate binding slots. */
	service_SDL_events(true);

	for (player_index = 0; player_index < MAX_PLAYERS; player_index++)
	{
		for (key_index = 0; key_index < MAX_KEY; key_index++)
		{
			for (slot_index = 0; slot_index < MAX_KEY_OPTIONS; slot_index++)
			{
				key = destruct_player[player_index].keys.Config[key_index][slot_index];
				if (key == SDL_SCANCODE_UNKNOWN)
					break;
				if (keysactive[key] == true)
				{
					destruct_player[player_index].moves.actions[key_index] = true;

					/* Consume edge-triggered actions after recording them. */
					if (key_index == KEY_CHANGE ||
						key_index == KEY_CYUP ||
						key_index == KEY_CYDN)
					{
						keysactive[key] = false;
					}
					break;
				}
			}
		}
	}

	// Controller support: map the pad to each human player's destruct moves. Destruct is
	// otherwise keyboard-only, so this is the only way to play it on the Switch (no keyboard).
	// One controller drives every human player (a 2-player match would need two pads).
	if (joysticks > 0)
	{
		poll_joystick(0);

		// Pause quits the minigame; there is no keyboard Escape on the Switch.
		if (joystick[0].action_pressed[5])
			keysactive[SDL_SCANCODE_ESCAPE] = true;

		for (player_index = 0; player_index < MAX_PLAYERS; player_index++)
		{
			if (destruct_player[player_index].is_cpu)
				continue;

			bool *act = destruct_player[player_index].moves.actions;
			if (joystick[0].direction[3])      act[KEY_LEFT]   = true;  // aim left  / move left
			if (joystick[0].direction[1])      act[KEY_RIGHT]  = true;  // aim right / move right
			if (joystick[0].direction[0])      act[KEY_UP]     = true;  // more power
			if (joystick[0].direction[2])      act[KEY_DOWN]   = true;  // less power
			// Ignore fire while a weapon-cycle action is held, preventing dual-bound
			// controller buttons from firing during a cycle.
			bool cycling = joystick[0].action[2] || joystick[0].action[3];
			if (joystick[0].action[0] && !cycling) act[KEY_FIRE] = true;  // fire (held)
			if (joystick[0].action_pressed[1]) act[KEY_CHANGE] = true;  // change unit (tap)
			if (joystick[0].action_pressed[2]) act[KEY_CYDN]   = true;  // previous weapon (tap)
			if (joystick[0].action_pressed[3]) act[KEY_CYUP]   = true;  // next weapon (tap)
		}
	}

	// Touch: one gather for the tick, then fanned out to every human player the same way
	// the pad above is.
	const Uint8 touch = DE_TouchActions();
	if (touch != 0)
	{
		for (player_index = 0; player_index < MAX_PLAYERS; player_index++)
		{
			if (destruct_player[player_index].is_cpu)
				continue;

			for (key_index = 0; key_index < MAX_KEY; key_index++)
			{
				if (touch & (1 << key_index))
					destruct_player[player_index].moves.actions[key_index] = true;
			}
		}
	}
}

static void DE_ProcessInput(void)
{
	int direction;

	unsigned int player_index;
	struct destruct_unit_s* curUnit;

	for (player_index = 0; player_index < MAX_PLAYERS; player_index++)
	{
		if (destruct_player[player_index].unitsRemaining <= 0)
			continue;

		direction = (player_index == PLAYER_LEFT) ? -1 : 1;
		curUnit = &(destruct_player[player_index].unit[destruct_player[player_index].unitSelected]);

		if (systemAngle[curUnit->unitType] == true) /* selected unit may change shot angle */
		{
			if (destruct_player[player_index].moves.actions[MOVE_LEFT] == true)
			{
				if (player_index == PLAYER_LEFT)
					DE_RaiseAngle(curUnit);
				else
					DE_LowerAngle(curUnit);
			}
			if (destruct_player[player_index].moves.actions[MOVE_RIGHT] == true)
			{
				if (player_index == PLAYER_LEFT)
					DE_LowerAngle(curUnit);
				else
					DE_RaiseAngle(curUnit);
			}
		}
		else if (curUnit->unitType == UNIT_HELI)
		{
			if (destruct_player[player_index].moves.actions[MOVE_LEFT] == true && curUnit->unitX > 5)
			{
				if (JE_stabilityCheck(curUnit->unitX - 5, roundf(curUnit->unitY)))
				{
					if (curUnit->lastMove > -5)
						curUnit->lastMove--;
					curUnit->unitX--;
					if (JE_stabilityCheck(curUnit->unitX, roundf(curUnit->unitY)))
						curUnit->isYInAir = true;
				}
			}
			if (destruct_player[player_index].moves.actions[MOVE_RIGHT] == true && curUnit->unitX < vga_width - 15)
			{
				if (JE_stabilityCheck(curUnit->unitX + 5, roundf(curUnit->unitY)))
				{
					if (curUnit->lastMove < 5)
						curUnit->lastMove++;
					curUnit->unitX++;
					if (JE_stabilityCheck(curUnit->unitX, roundf(curUnit->unitY)))
						curUnit->isYInAir = true;
				}
			}
		}

		if (curUnit->unitType != UNIT_LASER)
		{	/*increasepower*/
			if (destruct_player[player_index].moves.actions[MOVE_UP] == true)
			{
				if (curUnit->unitType == UNIT_HELI)
				{
					curUnit->isYInAir = true;
					curUnit->unitYMov -= 0.1f;
				}
				else if (curUnit->unitType == UNIT_JUMPER &&
					curUnit->isYInAir == false)
				{
					curUnit->unitYMov = -3;
					curUnit->isYInAir = true;
				}
				else
				{
					DE_RaisePower(curUnit);
				}
			}
			/*decreasepower*/
			if (destruct_player[player_index].moves.actions[MOVE_DOWN] == true)
			{
				if (curUnit->unitType == UNIT_HELI && curUnit->isYInAir == true)
				{
					curUnit->unitYMov += 0.1f;
				}
				else
				{
					DE_LowerPower(curUnit);
				}
			}
		}

		/*up/down weapon.  These just cycle until a valid weapon is found */
		if (destruct_player[player_index].moves.actions[MOVE_CYUP] == true)
			DE_CycleWeaponUp(curUnit);
		if (destruct_player[player_index].moves.actions[MOVE_CYDN] == true)
			DE_CycleWeaponDown(curUnit);

		/* Change.  Since change would change out curUnit pointer, let's just do it last.
		 * Validity checking is performed at the beginning of the tick. */
		if (destruct_player[player_index].moves.actions[MOVE_CHANGE] == true)
		{
			destruct_player[player_index].unitSelected++;
			if (destruct_player[player_index].unitSelected >= config.max_installations)
				destruct_player[player_index].unitSelected = 0;
		}

		/*Newshot*/
		if (destruct_player[player_index].shotDelay > 0)
			destruct_player[player_index].shotDelay--;
		if (destruct_player[player_index].moves.actions[MOVE_FIRE] == true &&
			destruct_player[player_index].shotDelay == 0)
		{
			destruct_player[player_index].shotDelay = shotDelay[curUnit->shotType];

			switch (shotDirt[curUnit->shotType])
			{
			case EXPL_NONE:
				break;

			case EXPL_MAGNET:
				DE_RunMagnet(player_index, curUnit);
				break;

			case EXPL_DIRT:
			case EXPL_NORMAL:
				DE_MakeShot(player_index, curUnit, direction);
				break;

			default:
				assert(false);
			}
		}
	}
}

// Both cyclers step an int and write the enum back once at the end; nothing reads unit->shotType
// inside the loop. Keeping the wrap on a local puts the bound on weaponSystems[][MAX_SHOT_TYPES]
// at the subscript itself, rather than on the enum staying inside [SHOT_FIRST, SHOT_LAST], which
// SHOT_INVALID = -1 can break.
static void DE_CycleWeaponUp(struct destruct_unit_s* unit)
{
	int type = unit->shotType;
	do
	{
		if (++type > SHOT_LAST)
			type = SHOT_FIRST;
	} while (weaponSystems[unit->unitType][type] == 0);
	unit->shotType = (enum de_shot_t)type;
}

static void DE_CycleWeaponDown(struct destruct_unit_s* unit)
{
	int type = unit->shotType;
	do
	{
		if (--type < SHOT_FIRST)
			type = SHOT_LAST;
	} while (weaponSystems[unit->unitType][type] == 0);
	unit->shotType = (enum de_shot_t)type;
}

static void DE_MakeShot(enum de_player_t curPlayer, const struct destruct_unit_s* curUnit, int direction)
{
	unsigned int i;
	unsigned int shotIndex;

	/* Find an available shot slot. */
	for (i = 0; ; i++)
	{
		if (i >= config.max_shots)
			return;  /* no empty slots.  Do nothing. */

		if (shotRec[i].isAvailable)
		{
			shotIndex = i;
			break;
		}
	}

	/* Helis can't fire when they are on the ground. */
	if (curUnit->unitType == UNIT_HELI && curUnit->isYInAir == false)
		return;

	/* Play the firing sound */
	soundQueue[curPlayer] = shotSound[curUnit->shotType];

	/* Create our shot.  Some units have differing logic here */
	switch (curUnit->unitType)
	{
	case UNIT_HELI:

		shotRec[shotIndex].x = curUnit->unitX + curUnit->lastMove * 2 + 5;
		shotRec[shotIndex].xmov = 0.02f * curUnit->lastMove * curUnit->lastMove * curUnit->lastMove;

		/* Handle upward movement at the top edge separately. */
		if (destruct_player[curPlayer].moves.actions[MOVE_UP] && curUnit->unitY < 30)
		{
			shotRec[shotIndex].y = curUnit->unitY;
			shotRec[shotIndex].ymov = 0.1f;

			if (shotRec[shotIndex].xmov < 0)
				shotRec[shotIndex].xmov += 0.1f;
			else if (shotRec[shotIndex].xmov > 0)
				shotRec[shotIndex].xmov -= 0.1f;
		}
		else
		{
			shotRec[shotIndex].y = curUnit->unitY + 1;
			shotRec[shotIndex].ymov = 0.5f + curUnit->unitYMov * 0.1f;
		}
		break;

	case UNIT_JUMPER: /* Jumpers are normally only special for the left hand player.  Bug?  Or feature? */

		if (config.jumper_straight[curPlayer])
		{
			/* Same trajectory as the default jumper. */

			shotRec[shotIndex].x = curUnit->unitX + 6 - sim_cosf(curUnit->angle) * 10 * direction;
			shotRec[shotIndex].y = curUnit->unitY - 7 - sim_sinf(curUnit->angle) * 10;
			shotRec[shotIndex].xmov = -sim_cosf(curUnit->angle) * curUnit->power * direction;
			shotRec[shotIndex].ymov = -sim_sinf(curUnit->angle) * curUnit->power;
		}
		else
		{
			/* This is not identical to the default case. */

			shotRec[shotIndex].x = curUnit->unitX + 2;
			shotRec[shotIndex].xmov = -sim_cosf(curUnit->angle) * curUnit->power * direction;

			if (curUnit->isYInAir == true)
			{
				shotRec[shotIndex].ymov = 1;
				shotRec[shotIndex].y = curUnit->unitY + 2;
			}
			else
			{
				shotRec[shotIndex].ymov = -2;
				shotRec[shotIndex].y = curUnit->unitY - 12;
			}
		}
		break;

	default:

		shotRec[shotIndex].x = curUnit->unitX + 6 - sim_cosf(curUnit->angle) * 10 * direction;
		shotRec[shotIndex].y = curUnit->unitY - 7 - sim_sinf(curUnit->angle) * 10;
		shotRec[shotIndex].xmov = -sim_cosf(curUnit->angle) * curUnit->power * direction;
		shotRec[shotIndex].ymov = -sim_sinf(curUnit->angle) * curUnit->power;
		break;
	}

	/* Now set/clear out a few last details. */
	shotRec[shotIndex].isAvailable = false;

	/* A freshly-fired shot has no previous position; anchor it where it spawned so
	 * the smooth present holds it still until it actually moves next tick. */
	shotRec[shotIndex].prev_x = shotRec[shotIndex].x;
	shotRec[shotIndex].prev_y = shotRec[shotIndex].y;

	shotRec[shotIndex].shottype = curUnit->shotType;
	shotRec[shotIndex].trailc[0] = 0;
	shotRec[shotIndex].trailc[1] = 0;
	shotRec[shotIndex].trailc[2] = 0;
	shotRec[shotIndex].trailc[3] = 0;
}

static void DE_RunMagnet(enum de_player_t curPlayer, struct destruct_unit_s* magnet)
{
	unsigned int i;
	enum de_player_t curEnemy;
	int direction;
	struct destruct_unit_s* enemyUnit;

	curEnemy = (curPlayer == PLAYER_LEFT) ? PLAYER_RIGHT : PLAYER_LEFT;
	direction = (curPlayer == PLAYER_LEFT) ? -1 : 1;

	/* Push all shots that are in front of the magnet */
	for (i = 0; i < config.max_shots; i++)
	{
		if (shotRec[i].isAvailable == false)
		{
			if ((curPlayer == PLAYER_LEFT && shotRec[i].x > magnet->unitX) ||
				(curPlayer == PLAYER_RIGHT && shotRec[i].x < magnet->unitX))
			{
				shotRec[i].xmov += magnet->power * 0.1f * -direction;
			}
		}
	}

	enemyUnit = destruct_player[curEnemy].unit;
	for (i = 0; i < config.max_installations; i++, enemyUnit++) /* magnets push coptors */
	{
		if (DE_isValidUnit(enemyUnit) &&
			enemyUnit->unitType == UNIT_HELI &&
			enemyUnit->isYInAir == true)
		{
			if ((curEnemy == PLAYER_RIGHT && destruct_player[curEnemy].unit[i].unitX + 11 < vga_width - 2) ||
				(curEnemy == PLAYER_LEFT && destruct_player[curEnemy].unit[i].unitX > 1))
			{
				enemyUnit->unitX -= 2 * direction;
			}
		}
	}
	magnet->ani_frame = 1;
}

static void DE_RaiseAngle(struct destruct_unit_s* unit)
{
	unit->angle += 0.01f;
	if (unit->angle > M_PI_2 - 0.01f)
		unit->angle = M_PI_2 - 0.01f;
}

static void DE_LowerAngle(struct destruct_unit_s* unit)
{
	unit->angle -= 0.01f;
	if (unit->angle < 0)
		unit->angle = 0;
}

static void DE_RaisePower(struct destruct_unit_s* unit)
{
	unit->power += 0.05f;
	if (unit->power > 5)
		unit->power = 5;
}

static void DE_LowerPower(struct destruct_unit_s* unit)
{
	unit->power -= 0.05f;
	if (unit->power < 1)
		unit->power = 1;
}

/* Health also serves as the unit-validity marker. */
static inline bool DE_isValidUnit(struct destruct_unit_s* unit)
{
	return unit->health > 0;
}

static bool DE_RunTickCheckEndgame(void)
{
	if (destruct_player[PLAYER_LEFT].unitsRemaining == 0)
	{
		destruct_player[PLAYER_RIGHT].score += ModeScore[PLAYER_LEFT][world.destructMode];
		soundQueue[7] = V_CLEARED_PLATFORM;
		return true;
	}
	if (destruct_player[PLAYER_RIGHT].unitsRemaining == 0)
	{
		destruct_player[PLAYER_LEFT].score += ModeScore[PLAYER_RIGHT][world.destructMode];
		soundQueue[7] = V_CLEARED_PLATFORM;
		return true;
	}
	return false;
}

static void DE_RunTickPlaySounds(void)
{
	unsigned int i, tempSampleIndex, tempVolume;

	for (i = 0; i < COUNTOF(soundQueue); i++)
	{
		if (soundQueue[i] != S_NONE)
		{
			tempSampleIndex = soundQueue[i];
			if (i == 7)
				tempVolume = fxPlayVol;
			else
				tempVolume = fxPlayVol / 2;

			multiSamplePlay(soundSamples[tempSampleIndex - 1], soundSampleCount[tempSampleIndex - 1], i, tempVolume);
			soundQueue[i] = S_NONE;
		}
	}
}

static void JE_pixCool(unsigned int x, unsigned int y, Uint8 c)
{
	JE_pix(VGAScreen, x, y, c);
	JE_pix(VGAScreen, x - 1, y, c - 2);
	JE_pix(VGAScreen, x + 1, y, c - 2);
	JE_pix(VGAScreen, x, y - 1, c - 2);
	JE_pix(VGAScreen, x, y + 1, c - 2);
}
