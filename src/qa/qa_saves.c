#include "qa.h"
#include "qa_internal.h"

#include "config.h"
#include "custom_weapon.h"
#include "endless.h"
#include "episodes.h"
#include "explosions.h"
#include "game_menu.h"   // JE_getLevelSections
#include "game_state.h"
#include "net_style.h"
#include "network.h"
#include "player.h"
#include "render_list.h"
#include "save_menu.h"
#include "savegame.h"
#include "shots.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void qa_test_config_option_removal(void)
{
	Config config = {0};
	ConfigSection *section = config_find_or_add_section(&config, "test", NULL);
	qa_check(section != NULL, "config option removal creates its test section");
	if (section != NULL)
	{
		config_set_option(section, "first", "1");
		config_set_option(section, "retired", "2");
		config_set_option(section, "last", "3");

		qa_check(config_remove_option(section, "retired") && section->options_count == 2 &&
		             config_get_option(section, "first") != NULL && config_get_option(section, "retired") == NULL &&
		             config_get_option(section, "last") != NULL,
		         "config option removal compacts a middle entry");
		qa_check(!config_remove_option(section, "missing") && section->options_count == 2,
		         "config option removal leaves a missing key unchanged");
		qa_check(config_remove_option(section, "first") && config_remove_option(section, "last") &&
		             section->options_count == 0 && section->options == NULL,
		         "config option removal releases an empty option array");
	}

	config_deinit(&config);
	qa_check(config.sections_count == 0 && config.sections == NULL, "config teardown resets section ownership");
}

void qa_test_weapon_editor(void)
{
	static char encoded[32768], encodedAgain[32768], malformed[4096];
	Uint32 rng = 0xc0570e11u;

	customWeaponResetAllLevels();
	bool presetsValid = customBulletPresetCount > 0 && customBulletPresetCount <= CUSTOM_BULLET_PRESET_MAX;
	for (int i = 0; i < customBulletPresetCount; ++i)
	{
		const CustomBulletPreset *const p = &customBulletPreset[i];
		presetsValid &= p->name[0] != '\0' && memchr(p->name, '\0', sizeof(p->name)) != NULL
		             && customBulletMaxPower(i) >= 1
		             && customBulletMaxPower(i) <= CUSTOM_POWER_LEVELS
		             && ((p->sourcePort >= 1 && p->sourcePort <= PORT_NUM)
		                 || (p->sourcePort == 0 && p->sourceWeapon >= 1 && p->sourceWeapon <= WEAP_NUM));
		for (int j = 0; j < i; ++j)
			presetsValid &= SDL_strcasecmp(p->name, customBulletPreset[j].name) != 0;
	}
	qa_check(presetsValid,
	         "weapon import registry is bounded, valid, terminated, and deduplicated");
	qa_check(customBulletMaxPower(-1) == 1
	         && customBulletMaxPower(customBulletPresetCount) == 1,
	         "weapon import registry safely handles invalid source indices");
	customWeaponSelectMode(-1000);
	customWeaponSelectLevel(-1000);
	qa_check(customWeaponEditMode == 0 && customWeaponEditLevel == 0,
	         "weapon editor clamps negative selections");
	customWeaponSelectMode(1000);
	customWeaponSelectLevel(1000);
	qa_check(customWeaponEditMode == CUSTOM_WEAPON_MODES - 1
	         && customWeaponEditLevel == CUSTOM_POWER_LEVELS - 1,
	         "weapon editor clamps oversized selections");

	customWeaponSelectMode(0);
	customWeaponSelectLevel(0);
	customWeaponSerializeLevel(0, 0, encoded, sizeof(encoded));
	customWeaponDeserializeLevel(0, 0, encoded);
	customWeaponSerializeLevel(0, 0, encodedAgain, sizeof(encodedAgain));
	qa_check(strcmp(encoded, encodedAgain) == 0,
	         "weapon editor serialization round-trips exactly");

	/* Exercise the config/editor parser with deterministic junk and huge integers. */
	static const char alphabet[] = "0123456789,+- xyz";
	for (unsigned pass = 0; pass < 256; ++pass)
	{
		const size_t n = qa_prng(&rng) % (sizeof(malformed) - 1);
		for (size_t i = 0; i < n; ++i)
			malformed[i] = alphabet[qa_prng(&rng) % (sizeof(alphabet) - 1)];
		malformed[n] = '\0';
		const int mode = (int)(pass % CUSTOM_WEAPON_MODES);
		const int level = (int)(pass % CUSTOM_POWER_LEVELS);
		customWeaponDeserializeLevel(mode, level, malformed);
		const JE_WeaponType *w = &customWeaponRaw[mode][level];
		qa_check(w->multi >= 1 && w->multi <= CUSTOM_BULLETS_MAX
		         && w->max >= 1 && w->max <= CUSTOM_BULLETS_MAX
		         && w->sound <= CUSTOM_SOUND_MAX,
		         "weapon editor rejects malformed values outside engine bounds");
	}

	customWeaponReset();
	int selected = 0;
	while ((selected = customWeaponAddBullet(selected)) >= 0)
		;
	qa_check(customWeaponRaw[0][0].multi == CUSTOM_BULLETS_MAX
	         && customWeaponAddBullet(0) == -1,
	         "weapon editor bullet pool stops at its exact capacity");
	while ((selected = customWeaponRemoveBullet(0)) >= 0)
		;
	qa_check(customWeaponRaw[0][0].multi == 1 && customWeaponRemoveBullet(0) == -1,
	         "weapon editor bullet pool keeps one valid segment");

	customWeaponChargeStages = 1;
	while (customWeaponAddChargeState() >= 0)
		;
	qa_check(customWeaponChargeStages == CUSTOM_POWER_LEVELS,
	         "weapon editor charge pool stops at its exact capacity");
	while (customWeaponRemoveChargeState() >= 0)
		;
	qa_check(customWeaponChargeStages == 1,
	         "weapon editor charge pool keeps one valid stage");

	customWeaponModes = 999;
	customWeaponCost = 1000000;
	customWeaponPowerUse = 1000000;
	customWeaponItemGraphic = 1000000;
	customWeaponChargeStages = 999;
	customSidekickMount = 999;
	customSidekickSprite = 1000000;
	customSidekickFrames = 999;
	customSidekickFrameStep = 999;
	customSidekickAnimate = 999;
	customWeaponMaterialize();
	if (customWeaponPort > 0 && customWeaponPort <= PORT_NUM
	 && customSidekickSlot > 0 && customSidekickSlot <= OPTION_NUM)
	{
		const JE_OptionType *const o = &options[customSidekickSlot];
		const int spriteCount = customSidekickSpriteCount(CUSTOM_SIDEKICK_MOUNTS - 1);
		const int spriteHi = spriteCount > CUSTOM_POWER_LEVELS - 1
		                   ? spriteCount - (CUSTOM_POWER_LEVELS - 1) : 1;
		bool framesSafe = true;
		for (int i = 0; i < 20; ++i)
			framesSafe &= o->gr[i] >= 1 && o->gr[i] <= spriteHi;
		qa_check(customWeaponModes == CUSTOM_WEAPON_MODES
		         && weaponPort[customWeaponPort].cost == 64000
		         && weaponPort[customWeaponPort].poweruse == 255
		         && weaponPort[customWeaponPort].itemgraphic == 237,
		         "weapon materialization clamps hostile port metadata to engine field bounds");
		qa_check(o->pwr == CUSTOM_POWER_LEVELS - 1 && o->tr == CUSTOM_SIDEKICK_MOUNTS - 1
		         && o->option == 2 && o->ani == 20 && o->cost == 64000 && framesSafe,
		         "sidekick materialization keeps charge and every sprite frame in its sheet");
	}
	else
	{
		qa_check(false, "custom weapon test slots were reserved during startup");
	}

	/* The in-memory library is another fixed pool. Do not involve the user's config file. */
	customWeaponLibCount = 1;
	customWeaponCurrentSlot = 0;
	customWeaponSelectSlot(-999);
	qa_check(customWeaponCurrentSlot == 0,
	         "weapon library clamps a negative slot selection");
	while (customWeaponLibraryNew() >= 0)
		;
	qa_check(customWeaponLibCount == CUSTOM_WEAPON_LIB_MAX
	         && customWeaponLibraryNew() == -1 && customWeaponLibraryDuplicate() == -1,
	         "weapon library stops exactly at its fixed capacity");
	customWeaponSelectSlot(999);
	qa_check(customWeaponCurrentSlot == CUSTOM_WEAPON_LIB_MAX - 1,
	         "weapon library clamps an oversized slot selection");
	while (customWeaponLibraryDelete() >= 0)
		;
	qa_check(customWeaponLibCount == 1 && customWeaponLibraryDelete() == -1,
	         "weapon library refuses to delete its final valid entry");

	/* Materialize every fuzzed level so sanitizer runs cover engine-array writes too. */
	customWeaponMaterialize();
}

/* Online Campaign designs must round-trip exactly and reject malformed streams. */
void qa_test_custom_weapon_wire(void)
{
	const int owner = CUSTOM_WEAPON_OWNERS - 1;
	const int port = customWeaponOwnerPort[owner];
	if (port <= 0 || port > PORT_NUM)
	{
		qa_check(false, "custom weapon owner slots were reserved during startup");
		return;
	}

	customWeaponResetAllLevels();
	SDL_strlcpy(customWeaponName, "QA Wire Gun", sizeof(customWeaponName));
	customWeaponCost = 12500;
	customWeaponPowerUse = 7;
	customWeaponModes = CUSTOM_WEAPON_MODES;
	customWeaponChargeStages = 4;
	customWeaponRaw[0][0].multi = 3;
	customWeaponRaw[0][0].max = 3;
	customWeaponRaw[0][0].sy[2] = -9;   /* signed field: the wire carries bytes */
	customWeaponRaw[0][0].sg[2] = 1234;
	customWeaponRaw[1][10].attack[0] = 99;
	customWeaponMaterialize();

	Uint8 *const stream = malloc(CUSTOM_WEAPON_WIRE_MAX);
	if (stream == NULL)
	{
		qa_check(false, "custom weapon wire buffer allocation");
		return;
	}

	const size_t total = customWeaponSerializeDesign(stream, CUSTOM_WEAPON_WIRE_MAX);
	qa_check(total > 0 && total <= CUSTOM_WEAPON_WIRE_MAX,
	         "custom weapon design encodes inside its declared wire bound");

	qa_check(customWeaponSerializeDesign(stream, 8) == 0,
	         "custom weapon encoder reports failure instead of running past a short buffer");

	const size_t reencoded = customWeaponSerializeDesign(stream, CUSTOM_WEAPON_WIRE_MAX);
	qa_check(reencoded == total && customWeaponAdoptDesign(owner, stream, total),
	         "custom weapon design decodes into the other player's reserved slots");

	const int scratch = CUSTOM_WEAP_BASE + owner * CUSTOM_WEAPON_MODES * CUSTOM_POWER_LEVELS;
	qa_check(weaponPort[port].opnum == CUSTOM_WEAPON_MODES
	         && weaponPort[port].cost == 12500 && weaponPort[port].poweruse == 7
	         && weapons[scratch].multi == 3 && weapons[scratch].sy[2] == -9
	         && weapons[scratch].sg[2] == 1234
	         && weapons[scratch + CUSTOM_POWER_LEVELS + 10].attack[0] == 99,
	         "adopted design reaches the port and every scratch weapon slot intact");

	// A peer's weapon fires through its own reserved port, and the record mark must still see it.
	qa_check(customWeaponPortIsCustom((JE_word)customWeaponOwnerPort[0])
	         && customWeaponPortIsCustom((JE_word)port)
	         && !customWeaponPortIsCustom(1),
	         "the fired-a-custom-weapon test covers every owner's port, not just the local one");

	qa_check(!customWeaponAdoptDesign(owner, stream, total / 2)
	         && !customWeaponAdoptDesign(owner, stream, 2)
	         && !customWeaponAdoptDesign(-1, stream, total)
	         && !customWeaponAdoptDesign(CUSTOM_WEAPON_OWNERS, stream, total),
	         "custom weapon decoder refuses a truncated stream and an out-of-range owner");

	stream[0] = CUSTOM_WEAPON_WIRE_VERSION + 1;
	qa_check(!customWeaponAdoptDesign(owner, stream, total),
	         "custom weapon decoder refuses an unknown wire version");

	free(stream);

	// Leave the reserved port as the item data had it; nothing else in this run owns it.
	customWeaponResetAllLevels();
	customWeaponMaterialize();
}

void qa_test_fixed_pool_layout(void)
{
	qa_check(RL_ID_ENEMY_BASE + (int)COUNTOF(enemy) <= RL_ID_ENEMYBAR_BASE
	         && RL_ID_ENEMYBAR_BASE + (int)COUNTOF(enemy) <= RL_ID_PSHOT_BASE
	         && RL_ID_PSHOT_BASE + MAX_PWEAPON <= RL_ID_ESHOT_BASE
	         && RL_ID_ESHOT_BASE + ENEMY_SHOT_MAX <= RL_ID_EXPL_BASE
	         && RL_ID_EXPL_BASE + MAX_EXPLOSIONS * 4 <= RL_ID_SHIP_BASE,
	         "render identities leave non-overlapping headroom for every fixed object pool");
	qa_check(RL_ID_SHIP_BASE + 2 <= RL_ID_SHIP_TRIM_BASE
	         && RL_ID_SHIP_TRIM_BASE + 2 <= RL_ID_SIDEKICK_BASE
	         && RL_ID_SIDEKICK_BASE + 4 <= RL_ID_LINKGUN_BASE
	         && RL_ID_LINKGUN_BASE + 3 <= RL_ID_SHIP_SHADOW_BASE
	         && RL_ID_SHIP_SHADOW_BASE + 3 < RL_ID_MAX,
	         "render identities for ships, sidekicks, and link guns remain disjoint");
	qa_check(sizeof(PlayerItems) == 13 && SAVE_RECORD_PACKED_SIZE == 162
	         && NETWORK_SETTINGS_SIZE == 48,
	         "network wire-layout constants retain their protocol widths");
}

void qa_test_save_record_wire(void)
{
	JE_SaveFileType src, dst;
	Uint8 guarded[SAVE_RECORD_PACKED_SIZE + 2];
	Uint8 repacked[SAVE_RECORD_PACKED_SIZE];
	Uint8 *const packed = guarded + 1;

	memset(&src, 0, sizeof(src));
	src.level = 0x1234;
	for (unsigned i = 0; i < sizeof(src.items); ++i)
	{
		src.items[i] = (JE_byte)(i * 7 + 1);
		src.lastItems[i] = (JE_byte)(255 - i * 9);
	}
	src.score = 4000000000LL;   // past the old 32-bit wallet
	src.score2 = 0x76543210;
	strcpy(src.levelName, "QA LEVEL");
	strcpy(src.name, "WIRE PLAYER");
	src.cubes = 17;
	src.power[0] = 4; src.power[1] = 11;
	src.episode = 5; src.difficulty = DIFFICULTY_LORD_OF_GAME;
	src.secretHint = 3; src.input1 = 1; src.input2 = 2;
	src.gameHasRepeated = true; src.initialDifficulty = DIFFICULTY_HARD;
	src.dualShips = SAVE_DUAL_COOP;
	src.dualPower[0] = 1; src.dualPower[1] = 2;
	src.dualMode[0] = 3; src.dualMode[1] = 4;
	src.autoFireSpecial = true; src.chargeSidekickAutofire = 2;
	src.difficultyAdjust = true; src.cheatInfiniteSidekickAmmo = true;
	src.cheatInfiniteShields = false; src.cheatInfiniteArmor = true; src.expertMode = true;
	src.shipColor[0] = 6; src.shipColor[1] = NET_SHIP_COLORS;
	src.viewOpacity[0] = NET_OPACITY_MIN; src.viewOpacity[1] = NET_OPACITY_FULL;
	src.viewShipOpacity[0] = 0; src.viewShipOpacity[1] = 1;
	src.viewHpBars[0] = NET_HP_BARS_ALWAYS; src.viewHpBars[1] = NET_HP_BARS_ON_HIT;
	strcpy(src.customEpFile, "qa_wire.clv");

	memset(guarded, 0xa5, sizeof(guarded));
	save_record_pack(packed, &src);
	qa_check(guarded[0] == 0xa5 && guarded[sizeof(guarded) - 1] == 0xa5,
	         "save-record packing writes exactly its fixed frame");
	save_record_unpack(&dst, packed);
	save_record_pack(repacked, &dst);
	qa_check(memcmp(packed, repacked, sizeof(repacked)) == 0,
	         "network save record pack/unpack round-trips every serialized field");
	qa_check(strcmp(dst.customEpFile, "qa_wire.clv") == 0,
	         "network save record carries the custom-episode container name");
	qa_check(dst.score == src.score && dst.score2 == src.score2 && dst.dualShips == src.dualShips
	         && memcmp(dst.dualPower, src.dualPower, sizeof(src.dualPower)) == 0
	         && memcmp(dst.dualMode, src.dualMode, sizeof(src.dualMode)) == 0,
	         "network save record carries 64-bit wallets and the dual-ship loadout");
	qa_check(save_record_is_coop(&dst),
	         "network save record preserves the Online Campaign type marker");
	qa_check(dst.shipColor[0] == src.shipColor[0] && dst.shipColor[1] == src.shipColor[1],
	         "network save record carries both ships' online dyes");
	qa_check(memcmp(dst.viewOpacity, src.viewOpacity, sizeof(src.viewOpacity)) == 0
	         && memcmp(dst.viewShipOpacity, src.viewShipOpacity, sizeof(src.viewShipOpacity)) == 0
	         && memcmp(dst.viewHpBars, src.viewHpBars, sizeof(src.viewHpBars)) == 0,
	         "...and both machines' views of the other ship, by seat");
	qa_check(dst.gameHasRepeated && dst.autoFireSpecial && dst.difficultyAdjust
	         && dst.cheatInfiniteSidekickAmmo && !dst.cheatInfiniteShields
	         && dst.cheatInfiniteArmor && dst.expertMode,
	         "network save record preserves all boolean gameplay flags");

	/* Hostile fixed-width strings still have to become safe C strings on receipt. */
	memset(packed + 47, 'L', 11);
	memset(packed + 58, 'N', 15);
	memset(packed + SAVE_RECORD_PACKED_SIZE - 64, 'C', 64);   // a shapeless custom name
	save_record_unpack(&dst, packed);
	qa_check(dst.levelName[sizeof(dst.levelName) - 1] == '\0'
	         && dst.name[sizeof(dst.name) - 1] == '\0',
	         "network save record terminates unterminated peer strings");
	qa_check(dst.customEpFile[0] == '\0',
	         "a custom-episode name that is not a plain *.clv file name is dropped on receipt");

	{
		const JE_boolean savedNet = isNetworkGame;
		JE_SaveFileType probe;
		memset(&probe, 0, sizeof(probe));
		probe.level = 1;

		isNetworkGame = false;
		const bool plainOffline = save_custom_locked(&probe);
		strcpy(probe.customEpFile, "qa_absent.clv");
		const bool missingOffline = save_custom_locked(&probe);
		isNetworkGame = true;
		const bool missingOnline = save_custom_locked(&probe);
		memset(probe.customEpFile, 0, sizeof(probe.customEpFile));
		strcpy(probe.customCollection, "qa_absent.clv");
		isNetworkGame = false;
		const bool collectionOffline = save_custom_locked(&probe);
		isNetworkGame = true;
		const bool collectionOnline = save_custom_locked(&probe);

		isNetworkGame = savedNet;
		qa_check(!plainOffline && missingOffline && collectionOffline
		         && !missingOnline && !collectionOnline,
		         "a save needing an absent container locks offline but stays open to peer sync");
	}
}

// The seat a resume hands back. Restored afterwards: these ride the player's own configuration.
void qa_test_save_slot_seats(void)
{
	uint saved[SAVE_FILES_NUM];
	for (uint i = 0; i < SAVE_FILES_NUM; ++i)
		saved[i] = save_slot_online_player((JE_byte)(i + 1));

	save_slot_set_online_player(22, 2);
	save_slot_set_online_player(12, 1);
	qa_check(save_slot_online_player(22) == 2 && save_slot_online_player(12) == 1,
	         "an online save slot remembers which player number wrote it");

	save_slot_set_online_player(22, 0);
	qa_check(save_slot_online_player(22) == 1,
	         "a local save over an online slot forgets its player number");

	save_slot_set_online_player(11, 2);
	save_slot_set_online_player(0, 2);
	save_slot_set_online_player(SAVE_FILES_NUM + 1, 2);
	qa_check(save_slot_online_player(11) == 1 && save_slot_online_player(0) == 1
	         && save_slot_online_player(SAVE_FILES_NUM + 1) == 1,
	         "seats are kept only for the two-player page an online session writes");

	for (uint i = 0; i < SAVE_FILES_NUM; ++i)
		save_slot_set_online_player((JE_byte)(i + 1), saved[i]);
}

/* Run preset coverage last because it temporarily changes the real settings. */
void qa_test_enhancement_presets(void)
{
	/* One setting per Enhancements screen and group. Each is poked away from the Engaged set the
	 * probe starts from, which leaves the table matching neither preset if the setting is listed. */
	static const struct
	{
		int *intSetting;    // exactly one of the two pointers is set, as in the preset table
		bool *boolSetting;
		int poke;
		const char *name;
	} probes[] = {
		{ .boolSetting = &extraParallax, .poke = true, .name = "Visuals" },
		{ .intSetting = &enemyBarOpacity, .poke = 40, .name = "Enemy Bars" },
		{ .intSetting = &bossBarLayout, .poke = BOSS_BAR_BOTTOM, .name = "Boss Bars" },
		{ .intSetting = &vulnerableCue, .poke = VULN_CUE_ALL, .name = "Vulnerable Cue" },
		{ .intSetting = &gaugeGradGenerator, .poke = GAUGE_GRAD_DOWN, .name = "Gauges" },
		{ .boolSetting = &gaugeFlashArmor, .poke = false, .name = "Gauge flash" },
		{ .boolSetting = &chargeLaserCannon, .poke = false, .name = "Charge-Laser" },
		{ .intSetting = &superSparkMode[SSW_ICE], .poke = SUPER_SPARKS_OFF, .name = "Spark Trails" },
		{ .intSetting = &wallopSecondBolt, .poke = SUPER_SPARKS_OFF, .name = "Wallop 2nd Bolt" },
		{ .boolSetting = &superSparkClassicCap[SSW_ICE], .poke = false, .name = "Classic Spark Caps" },
		{ .boolSetting = &centeredShotHitboxes, .poke = false, .name = "Gameplay" },
		{ .boolSetting = &guidedShotScreenAim, .poke = true, .name = "Guided Aim" },
		{ .boolSetting = &arcadeLifeBoost, .poke = false, .name = "Arcade Modes" },
		{ .intSetting = &epDiffMode[EDW_FLARE], .poke = EPDIFF_EP13, .name = "Item Data" },
		{ .intSetting = &zicaLaserLength, .poke = ZICA_LEN_LONG, .name = "Zica Laser" },
		{ .intSetting = &epDiffMode[EDW_NEEDLE_LASER], .poke = EPDIFF_EP13, .name = "Firing Sounds" },
		{ .intSetting = &epDiffMode[EDW_SOLAR_SHIELD], .poke = EPDIFF_EP13, .name = "Solar Shield icon" },
		{ .intSetting = &epDiffMode[EDW_USHIP_PIC], .poke = EPDIFF_EP45, .name = "Shop Pictures" },
	};

	/* Both presets have to land on themselves, or the Preset row would read Custom the moment
	 * one was applied. */
	const EnhancementPreset presets[] = { ENH_PRESET_VANILLA, ENH_PRESET_ENGAGED };
	for (size_t p = 0; p < COUNTOF(presets); ++p)
	{
		enhancementApplyPreset(presets[p]);
		qa_check(enhancementPresetState() == presets[p], "an applied preset reads back as itself");
	}

	// Enhancement presets must not change the Extra menu's Custom Weapons setting.
	customWeaponEnabled = false;
	enhancementApplyPreset(ENH_PRESET_ENGAGED);
	qa_check(!customWeaponEnabled, "the Engaged preset leaves the Extra custom-weapon toggle alone");
	customWeaponEnabled = true;
	enhancementApplyPreset(ENH_PRESET_VANILLA);
	qa_check(customWeaponEnabled, "the Vanilla preset leaves the Extra custom-weapon toggle alone");

	for (size_t i = 0; i < COUNTOF(probes); ++i)
	{
		enhancementApplyPreset(ENH_PRESET_ENGAGED);

		if (probes[i].intSetting != NULL)
			*probes[i].intSetting = probes[i].poke;
		else
			*probes[i].boolSetting = (probes[i].poke != 0);

		if (enhancementPresetState() != ENH_PRESET_CUSTOM)
			fprintf(stderr, "# %s does not reach the preset table\n", probes[i].name);
		qa_check(enhancementPresetState() == ENH_PRESET_CUSTOM,
		         "a changed setting turns the Preset row Custom");
	}

	/* Custom is a set the player keeps: it survives a trip through the other presets and comes
	 * back with the values that were changed by hand. */
	enhancementApplyPreset(ENH_PRESET_ENGAGED);
	*probes[0].boolSetting = (probes[0].poke != 0);
	const int handMade = *probes[1].intSetting = probes[1].poke;
	enhancementNoteCustom();
	qa_check(enhancementCustomAvailable(), "a hand-edited set is remembered as Custom");

	enhancementApplyPreset(ENH_PRESET_VANILLA);
	qa_check(enhancementPresetState() == ENH_PRESET_VANILLA, "a preset still overwrites that set");

	enhancementApplyPreset(ENH_PRESET_CUSTOM);
	qa_check(enhancementPresetState() == ENH_PRESET_CUSTOM
	         && *probes[0].boolSetting == (probes[0].poke != 0) && *probes[1].intSetting == handMade,
	         "...and Custom hands the whole set back");

	/* Editing from a preset replaces the remembered set rather than adding to it. */
	enhancementApplyPreset(ENH_PRESET_VANILLA);
	*probes[2].intSetting = probes[2].poke;
	enhancementNoteCustom();
	enhancementApplyPreset(ENH_PRESET_ENGAGED);
	enhancementApplyPreset(ENH_PRESET_CUSTOM);
	qa_check(*probes[2].intSetting == probes[2].poke && *probes[1].intSetting != handMade,
	         "the newest hand-edited set is the one Custom holds");

	enhancementApplyPreset(ENH_PRESET_ENGAGED);
	qa_check(enhancementPresetState() == ENH_PRESET_ENGAGED, "re-applying a preset clears Custom");
	customWeaponEnabled = true;
}

void qa_test_save_fixtures(void)
{
	char path[512];
	char detail[256];

	// The text record itself: every field survives, and a stray or missing key costs only itself.
	detail[0] = '\0';
	const bool codec = endlessSaveTestTextCodec(detail, sizeof(detail));
	char label[320];
	snprintf(label, sizeof(label), "endless text record codec%s%s", detail[0] ? ": " : "", detail);
	qa_check(codec, label);

	// The campaign slot codec, the same way.
	detail[0] = '\0';
	const bool slots = save_file_test_codec(detail, sizeof(detail));
	snprintf(label, sizeof(label), "campaign slot codec%s%s", detail[0] ? ": " : "", detail);
	qa_check(slots, label);

	// Customize is inserted without changing stock row mappings.
	detail[0] = '\0';
	const bool rows = game_menu_test_outpost_rows(detail, sizeof(detail));
	snprintf(label, sizeof(label), "online outpost rows%s%s", detail[0] ? ": " : "", detail);
	qa_check(rows, label);

	/* Import the upstream fixture, then restore the live save tables. */
	JE_SaveFilesType savedSlots;
	T2KHighScoreType savedBoards[20][3];
	memcpy(savedSlots, saveFiles, sizeof(savedSlots));
	memcpy(savedBoards, t2kHighScores, sizeof(savedBoards));

	snprintf(path, sizeof(path), "%s/upstream_tyrian.sav", qa_fixture_dir);
	qa_check(save_legacy_test_import(path),
	         "an upstream tyrian.sav passes its checksums and imports");
	qa_check(saveFiles[10].level == 1 && saveFiles[10].episode == 1 && saveFiles[10].score == 3900
	         && saveFiles[10].difficulty == DIFFICULTY_NORMAL && saveFiles[10].difficultyAdjust
	         && strcmp(saveFiles[10].name, "LAST LEVEL    ") == 0
	         && strcmp(saveFiles[10].levelName, "NEW GAME") == 0 && saveFiles[0].level == 0
	         && !save_record_is_coop(&saveFiles[11]) && !save_record_is_dual_arcade(&saveFiles[11]),
	         "the imported upstream slots carry their names, levels, episodes and cash");
	qa_check(t2kHighScores[0][0].score == 2400
	         && strcmp(t2kHighScores[0][0].playerName, "Calypso") == 0
	         && t2kHighScores[10][0].score == 12000
	         && strcmp(t2kHighScores[10][0].playerName, "Mantori") == 0
	         && t2kHighScores[19][1].score == 7000
	         && strcmp(t2kHighScores[19][1].playerName, "Team Vykromod") == 0,
	         "the imported upstream high-score boards carry their names and scores");

	memcpy(saveFiles, savedSlots, sizeof(savedSlots));
	memcpy(t2kHighScores, savedBoards, sizeof(savedBoards));
}
