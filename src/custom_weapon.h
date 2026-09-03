/* Custom weapons use editable JE_WeaponType designs in reserved engine slots. */
#ifndef CUSTOM_WEAPON_H
#define CUSTOM_WEAPON_H

#include "opentyr.h"
#include "episodes.h"

// Custom weapons carry one raw design for each of the 11 stock power levels.
#define CUSTOM_POWER_LEVELS 11

// Rear guns may define two fire modes; front guns use mode 0 only.
#define CUSTOM_WEAPON_MODES 2

// The editor exposes one segment for every slot in the engine's per-bullet arrays.
#define CUSTOM_BULLETS_MAX WEAPON_MULTI_MAX

// Online Campaign reserves independent weapon, sidekick, and scratch slots for each player.
#define CUSTOM_WEAPON_OWNERS 2

// Scratch weapon slots for the compiled custom designs: one per (owner, mode, level),
// CUSTOM_WEAP_BASE + (owner*CUSTOM_WEAPON_MODES + mode)*CUSTOM_POWER_LEVELS + (level-1).
#define CUSTOM_WEAP_BASE 910

// Highest sound sample the engine can play (see sndmast.h SFX_COUNT).
#define CUSTOM_SOUND_MAX 31

enum  // where the custom weapon equips
{
	CUSTOM_EQUIP_FRONT = 0,   // front weapon bay
	CUSTOM_EQUIP_REAR,        // rear weapon bay
	CUSTOM_EQUIP_LEFT,        // left sidekick
	CUSTOM_EQUIP_RIGHT,       // right sidekick
	CUSTOM_EQUIP_BOTH,        // both sidekicks
	CUSTOM_EQUIP_COUNT
};

// The raw editable design of each (mode, power level): the engine's own weapon
// struct. The editor reads/writes customWeaponRaw[editMode][editLevel] in place.
extern JE_WeaponType customWeaponRaw[CUSTOM_WEAPON_MODES][CUSTOM_POWER_LEVELS];

// How many fire modes the weapon has (1 or 2 = the port's opnum). A second mode
// is only reachable when the weapon is equipped as a rear gun (mode toggle).
extern int customWeaponModes;

// Weapon-wide identity. These map to the single reserved port (not to any one
// level or mode), so they are shared across all designs.
extern char customWeaponName[31];
extern int  customWeaponCost;       // shop price, 0 .. 64000
extern int  customWeaponPowerUse;   // generator drain (port poweruse)
extern int  customWeaponEquipSlot;  // CUSTOM_EQUIP_*
extern int  customWeaponItemGraphic; // shop/HUD icon (weaponPort itemgraphic), 1 .. 237

// Sidekick charge stages map to option pwr+1 and use consecutive mode-0 power levels.
// A value of 1 fires immediately; larger values charge through independently editable shots.
extern int  customWeaponChargeStages;

// Which power level / fire mode the editor is currently editing.
extern int customWeaponEditLevel;   // 0 .. CUSTOM_POWER_LEVELS-1
extern int customWeaponEditMode;    // 0 .. CUSTOM_WEAPON_MODES-1

// Master feature toggle (shows the "Custom" buy/sell row; gates equipping).
extern bool customWeaponEnabled;

// Port index claimed for this machine's own custom weapon (resolved at init). 0 = none free.
extern int customWeaponPort;

// Option (sidekick) slot claimed for this machine's own custom sidekick (0 = none free).
extern int customSidekickSlot;

// The reserved slots per owner. Index with the player index, not with local/remote: both
// machines must agree on which port carries which player's weapon. 0 = none free.
extern int customWeaponOwnerPort[CUSTOM_WEAPON_OWNERS];
extern int customSidekickOwnerSlot[CUSTOM_WEAPON_OWNERS];

// The owner index the editor, the preview and Equip write. Always 0 outside Online Campaign.
int customWeaponLocalOwner(void);

// True when the port / option slot is any owner's reserved custom one, local player's or a peer's.
bool customWeaponPortIsCustom(JE_word port);
bool customSidekickSlotIsCustom(int option);

// Sidekick body appearance. Mount style selects position and sprite sheet;
// animation and charge stages advance from Sprite by FrameStep.
#define CUSTOM_SIDEKICK_MOUNTS 5   // tr values 0..4 are all valid mount styles
extern int customSidekickMount;     // option tr (0..4)
extern int customSidekickSprite;    // base body sprite (gr[0]); sheet depends on the mount
extern int customSidekickFrames;    // animation frame count (ani; 1 = static)
extern int customSidekickFrameStep; // sprite step between animation frames (0/1 typical)
extern int customSidekickAnimate;   // option: 1 = animate while firing, 2 = always animate

// Largest body sprite a mount's sheet can address (so the editor + materialize can clamp
// the sprite index; the sidekick blit is not bounds-checked). 0 if the sheet isn't loaded.
int customSidekickSpriteCount(int mount);

// Named stock weapon and sidekick sources used by the editor's Import actions.
typedef struct
{
	char    name[31];      // source weapon/sidekick name shown in the picker
	JE_word sourcePort;    // weaponPort index (1..PORT_NUM) when it has power levels; 0 = sidekick-sourced
	JE_word sourceWeapon;  // weapon number to use when sourcePort == 0 (a sidekick's single shot)
	JE_word sourceOption;  // options[] index when sidekick-sourced (0 otherwise); lets Import clone the body
	// Highest sampleable stage, or a charge sidekick's shot count. Defaults to 1.
	JE_byte maxPower;
} CustomBulletPreset;

// Headroom for every unique source across all PORT_NUM (60) ports + OPTION_NUM (37)
// sidekicks; dedup keeps the real count well below this.
#define CUSTOM_BULLET_PRESET_MAX 112
extern CustomBulletPreset customBulletPreset[CUSTOM_BULLET_PRESET_MAX];
extern int                customBulletPresetCount;

// Number of base power levels the given import source can be sampled at (1..11);
// 1 means the look is fixed (a sidekick shot, or a weapon with a single level).
int customBulletMaxPower(int presetIdx);

// Initialize after JE_loadItemDat(); repeated calls preserve loaded designs.
void customWeaponInit(void);

// Copy every power level's raw design into this owner's scratch weapon slots and wire up its
// port and sidekick. Safe to call after every edit.
void customWeaponMaterialize(void);

// Equip the (freshly materialized) custom weapon into the player's front/rear bay
// or sidekick slot per customWeaponEquipSlot. Returns true on success.
bool customWeaponEquip(void);

// Switch which power level / fire mode the editor is editing (edits happen in
// place, so these are just clamped assignments; no save/load dance).
void customWeaponSelectLevel(int level);
void customWeaponSelectMode(int mode);

// Import one stock power level, or clone the full curve with its name and drain.
void customWeaponImportLevel(int presetIdx, int basePower);
void customWeaponImportAllLevels(int presetIdx);

// Append a stock weapon's bullet segments without replacing design metadata.
// Level affects the current power level; All maps all source levels onto the custom levels.
void customWeaponAddLevel(int presetIdx, int basePower);
void customWeaponAddAllLevels(int presetIdx);

// Restore the built-in default design to the current level / roll a random (but
// always valid) design into the current level.
void customWeaponReset(void);
void customWeaponRandomize(void);

// Add duplicates and nudges a bullet segment; remove compacts the array.
// Both return the next selected index, or -1 at their size limit.
int customWeaponAddBullet(int afterIndex);
int customWeaponRemoveBullet(int index);

// Add or remove the highest sidekick charge stage.
// Returns the selected top level, or -1 at the stage limit.
int customWeaponAddChargeState(void);
int customWeaponRemoveChargeState(void);

// Copy the edited level into all 11 levels, or restore the complete built-in default.
void customWeaponCopyToAllLevels(void);
void customWeaponResetAllLevels(void);

// Generate the other ten power levels around the level currently being edited.
void customWeaponAutoScaleLevels(void);

// Persistence helpers (used by config.c). One (mode, level) design serializes to a
// compact, whitespace-free comma-separated list of integers (safe for the parser).
void customWeaponSerializeLevel(int mode, int level, char *buf, size_t bufSize);
void customWeaponDeserializeLevel(int mode, int level, const char *str);

// The library keeps one working copy materialized in the reserved engine slots.
#define CUSTOM_WEAPON_LIB_MAX 32

typedef struct
{
	char name[31];
	int  cost, powerUse, equipSlot, itemGraphic, chargeStages, modes;
	int  sidekickMount, sidekickSprite, sidekickFrames, sidekickFrameStep, sidekickAnimate;
	JE_WeaponType raw[CUSTOM_WEAPON_MODES][CUSTOM_POWER_LEVELS];
} CustomWeaponSlot;

extern CustomWeaponSlot customWeaponLib[CUSTOM_WEAPON_LIB_MAX];
extern int customWeaponLibCount;    // number of saved weapons (always >= 1)
extern int customWeaponCurrentSlot; // which slot the working copy came from

// Switch which library weapon is being edited: stashes the working copy back to its slot,
// then loads the chosen one into the globals and materializes it.
void customWeaponSelectSlot(int slot);

// Add or duplicate and select a slot; delete always leaves at least one library entry.
int  customWeaponLibraryNew(void);
int  customWeaponLibraryDuplicate(void);
int  customWeaponLibraryDelete(void);

// Load / save the whole library file. Load is called once from customWeaponInit (seeds a
// single slot from the working copy when no file exists yet, migrating an old single weapon).
void customWeaponLibraryLoad(void);
bool customWeaponLibrarySave(void);

/* Serialize or replace the full library. Serialization first captures the active design. */
#define CUSTOM_WEAPON_LIBRARY_WIRE_VERSION 1
#define CUSTOM_WEAPON_LIBRARY_WIRE_MAX \
	(4 + CUSTOM_WEAPON_LIB_MAX * (4 + CUSTOM_WEAPON_WIRE_MAX))

size_t customWeaponSerializeLibrary(Uint8 *buf, size_t cap);
bool customWeaponAdoptLibrary(const Uint8 *buf, size_t len);

/* Exchange a versioned Campaign design; adoption clamps data and does not touch the editor. */
#define CUSTOM_WEAPON_WIRE_VERSION 1

// Upper bound on the encoded size: the fixed header plus every (mode, level) at full width.
#define CUSTOM_WEAPON_WIRE_MAX \
	(64 + CUSTOM_WEAPON_MODES * CUSTOM_POWER_LEVELS * (18 + 8 * CUSTOM_BULLETS_MAX))

size_t customWeaponSerializeDesign(Uint8 *buf, size_t cap);
bool customWeaponAdoptDesign(int owner, const Uint8 *buf, size_t len);

// Claim and compile the reserved slots both players need. Idempotent; call when a Campaign
// session reaches the outpost.
void customWeaponNetPrepare(void);

#endif // CUSTOM_WEAPON_H
