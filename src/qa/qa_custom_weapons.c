#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "config.h"
#include "custom_weapon.h"
#include "episodes.h"
#include "extra_ships.h"
#include "network.h"
#include "sprite.h"
#include "weapon_editor.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int qa_shot_sway2(JE_byte circlesize)
{
	const int size = (circlesize > 19) ? circlesize % 20 : circlesize;
	int dev = size / 2, dir = 1, pos = 0, lo = 0, hi = 0;
	for (int tick = 0; size > 0 && tick < 4 * size; ++tick)
	{
		dev += dir;
		pos += dev;
		if (abs(dev) == size)
			dir = -dir;
		lo = MIN(lo, pos);
		hi = MAX(hi, pos);
	}
	return lo + hi;
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
		bool cleared = true;
		for (int k = MAX(w->multi, w->max); k < WEAPON_MULTI_MAX; ++k)
			cleared = cleared && w->attack[k] == 0 && w->del[k] == 0 && w->sx[k] == 0 &&
			          w->sy[k] == 0 && w->bx[k] == 0 && w->by[k] == 0 && w->sg[k] == 0;
		qa_check(w->multi >= 1 && w->multi <= CUSTOM_BULLETS_MAX
		         && w->max >= 1 && w->max <= CUSTOM_BULLETS_MAX
		         && w->sound <= CUSTOM_SOUND_MAX && customWeaponTrailValid(w->trail)
		         && w->aim <= CUSTOM_AIM_MAX && cleared,
		         "weapon editor rejects malformed values outside engine bounds");
	}

	customWeaponSelectMode(0);
	customWeaponSelectLevel(0);
	customWeaponReset();
	JE_WeaponType *const stored = &customWeaponRaw[0][0];
	stored->multi = 1;
	stored->max = 1;
	stored->trail = 77;
	stored->aim = 200;
	stored->attack[3] = 55;
	stored->sg[3] = 47872;
	customWeaponMaterialize();
	qa_check(stored->trail == 255 && stored->aim == CUSTOM_AIM_MAX && stored->attack[3] == 0 &&
	         stored->sg[3] == 0,
	         "a design keeps a valid trail, a homing delay below Guidance, and its fired slots");

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

	int widePreset = -1, stockPreset = -1, beamPreset = -1;
	for (int i = 0; i < customBulletPresetCount; ++i)
	{
		if (strcmp(customBulletPreset[i].name, "Plasma Storm") == 0)
			widePreset = i;
		if (strcmp(customBulletPreset[i].name, "HotDog Front") == 0)
			stockPreset = i;
		if (strcmp(customBulletPreset[i].name, "Zica Laser") == 0)
			beamPreset = i;
	}
	Uint32 beamLayout[CUSTOM_SPREAD_COUNT];
	int beamLayouts = 0;
	for (int spread = CUSTOM_SPREAD_ARROW; spread < CUSTOM_SPREAD_COUNT; ++spread)
	{
		customWeaponSpread = spread;
		bool mirrored = true, bounded = true, rising = true, stacked = false;
		for (int pass = 0; pass < 3; ++pass)
		{
			const int source = (pass == 0) ? -1 : (pass == 1) ? widePreset : beamPreset;
			if (pass > 0 && source < 0)
				continue;
			if (source < 0)
				customWeaponResetAllLevels();
			else
				customWeaponImportAllLevels(source);
			customWeaponSelectMode(0);
			for (int anchor = 0; anchor < CUSTOM_POWER_LEVELS; anchor += CUSTOM_POWER_LEVELS - 1)
			{
				customWeaponSelectLevel(anchor);
				customWeaponAutoScaleLevels();
				int dps[CUSTOM_POWER_LEVELS];
				for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
				{
					const JE_WeaponType *const w = &customWeaponRaw[0][p];
					const int slots = (w->max > w->multi) ? w->max : w->multi;
					const int sway2 = qa_shot_sway2(w->circlesize);
					int centre[CUSTOM_BULLETS_MAX], cycle = 0, left = 999, right = -999;
					for (int i = 0; i < slots && i < CUSTOM_BULLETS_MAX; ++i)
					{
						const int width = (w->sg[i] > 60000)
						                ? get_sprite_width(OPTION_SHAPES, w->sg[i] - 60001)
						                : 12;
						const int c = 2 * w->bx[i] + sway2;
						int j = i;
						for (; j > 0 && centre[j - 1] > c; --j)
							centre[j] = centre[j - 1];
						centre[j] = c;
						left = MIN(left, w->bx[i] + 1);
						right = MAX(right, w->bx[i] + width);
						cycle += w->attack[i];
					}
					if (pass == 1)
						mirrored &= p == anchor || abs(left + right + sway2 - 13) <= 2;
					else if (source < 0 || p != anchor)
						for (int i = 0; i < slots && i < CUSTOM_BULLETS_MAX; ++i)
							mirrored &= abs(centre[i] + centre[slots - 1 - i]) <= (sway2 & 1) * 2;
					bounded &= w->multi >= 1 && slots <= CUSTOM_BULLETS_MAX &&
					           slots % w->multi == 0;
					dps[p] = cycle * w->multi * 35 / (slots * (w->shotrepeat + 1));
					const int multi = MAX(w->multi, 1), last = MIN(slots, CUSTOM_BULLETS_MAX);
					for (int i = 0; i < last && p != anchor; ++i)
						for (int j = i + 1; j < MIN((i / multi + 1) * multi, last); ++j)
							stacked |= w->sg[i] == w->sg[j] && w->bx[i] == w->bx[j] &&
							           w->by[i] == w->by[j] && w->sx[i] == w->sx[j] &&
							           w->sy[i] == w->sy[j];
				}
				if (source < 0)
					rising &= dps[CUSTOM_POWER_LEVELS - 1] > dps[0] * 4;
				if (pass == 2 && anchor == 0)
				{
					const JE_WeaponType *const top = &customWeaponRaw[0][CUSTOM_POWER_LEVELS - 1];
					const int topSlots = (top->max > top->multi) ? top->max : top->multi;
					Uint32 layout = 0;
					for (int i = 0; i < topSlots && i < CUSTOM_BULLETS_MAX; ++i)
						layout += (Uint32)((top->bx[i] + 128) * 2654435761u) ^
						          (Uint32)((top->by[i] + 128) * 40503u) ^
						          (Uint32)((top->sx[i] + 128) * 97u + (top->sy[i] + 128));
					bool seen = false;
					for (int k = 0; k < beamLayouts && !seen; ++k)
						seen = beamLayout[k] == layout;
					if (!seen)
						beamLayout[beamLayouts++] = layout;
				}
			}
		}
		qa_check(mirrored && bounded && rising && !stacked,
		         "auto-scale builds centred, bounded, rising, unstacked levels with every spread");
	}
	qa_check(beamPreset < 0 || beamLayouts >= 10,
	         "auto-scale spreads give a ship-locked laser distinct layouts");
	if (stockPreset >= 0)
	{
		customWeaponImportAllLevels(stockPreset);
		customWeaponSelectMode(0);
		customWeaponSelectLevel(0);
		customWeaponSpread = CUSTOM_SPREAD_STOCK;
		customWeaponAutoScaleLevels();
		const bool followed = customWeaponLastSpread == CUSTOM_SPREAD_STOCK;
		customWeaponSpread = CUSTOM_SPREAD_FAN;
		customWeaponAutoScaleLevels();
		const bool chosen = customWeaponLastSpread == CUSTOM_SPREAD_FAN;
		customWeaponSpread = CUSTOM_SPREAD_AUTO;
		bool varied = true;
		for (int press = 0; press < 8; ++press)
		{
			const int before = customWeaponLastSpread;
			customWeaponAutoScaleLevels();
			varied &= customWeaponLastSpread != before;
		}
		qa_check(followed && chosen && varied,
		         "auto-scale uses a chosen spread on stock weapons and Auto changes each press");
	}
	customWeaponSpread = CUSTOM_SPREAD_AUTO;

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
		         && weaponPort[customWeaponPort].itemgraphic == CUSTOM_ICON_MAX,
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

	bool sameHull = true;
	for (int ship = 1; ship <= SHIP_DRAGONWING; ++ship)
	{
		if (ships[ship].name[0] == '\0')
			continue;
		const int hull = weaponEditorRangeShip(ship);
		sameHull = sameHull && hull >= 1 && hull <= ship && ships[hull].name[0] != '\0' &&
		           ships[hull].shipgraphic == ships[ship].shipgraphic;
		for (int other = 1; other < hull && sameHull; ++other)
			sameHull = ships[other].name[0] == '\0' ||
			           ships[other].shipgraphic != ships[ship].shipgraphic;
	}
	qa_check(sameHull, "the outpost editor previews a ship as the first range ship with its hull");
	qa_check(strncmp(ships[7].name, "Prototype Stalker-C", 19) == 0 &&
	         strncmp(ships[5].name, "MicroCorp Stalker", 17) == 0 &&
	         weaponEditorRangeShip(7) == 5,
	         "the outpost editor previews a Prototype Stalker-C as the deduped MicroCorp Stalker");
	const char microCorpInitial = ships[5].name[0];
	ships[5].name[0] = '\0';
	const int unnamedFirst = weaponEditorRangeShip(7);
	ships[5].name[0] = microCorpInitial;
	qa_check(unnamedFirst == 6, "a ship with no name never stands in for its hull");
	qa_check(weaponEditorRangeShip(0) == -1 && weaponEditorRangeShip(SHIP_DRAGONWING + 1) == -1,
	         "the outpost editor keeps its range ship for an id with no hull");
	const int custom = EXTRA_SHIP_ID_BASE + 1;
	const JE_boolean savedExtraAvail = extraAvail;
	extraAvail = false;
	const int unlisted = weaponEditorRangeShip(custom);
	extraAvail = true;
	const int listed = weaponEditorRangeShip(custom);
	extraAvail = savedExtraAvail;
	qa_check(unlisted == -1 && listed == custom,
	         "the outpost editor previews a custom ship only while custom ships are listed");
	qa_check(weaponEditorOutpostMusicSelfTest(),
	         "the outpost weapon editor keeps the outpost's song; the Setup one shuffles songs");

	char detail[96] = "";
	char label[192];
	const bool wholeIcons = weaponEditorIconPickerSelfTest(detail, sizeof(detail));
	snprintf(label, sizeof(label),
	         "the Shop Icon picker offers and steps through whole icons only%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(wholeIcons, label);

	detail[0] = '\0';
	const bool tabs = weaponEditorTabNavSelfTest(detail, sizeof(detail));
	snprintf(label, sizeof(label),
	         "the editor's tabs move as a grid and Enter opens the page%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(tabs, label);
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
	SDL_strlcpy(customWeaponName, "QA\nWire\tGun", sizeof(customWeaponName));
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
	qa_check(strcmp(weaponPort[port].name, "QA Wire Gun") == 0,
	         "an adopted design's name loses its control characters");

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

	const bool savedNet = isNetworkGame;
	const JE_boolean savedCoop = coopCampaignMode;
	const uint savedSeat = thisPlayerNum;
	isNetworkGame = true;
	coopCampaignMode = true;
	thisPlayerNum = 1;
	customWeaponNetPrepare();
	qa_check(weaponPort[port].cost == 12500 && weapons[scratch].multi == 3,
	         "a refused design leaves the other player's last good design in place");
	isNetworkGame = savedNet;
	coopCampaignMode = savedCoop;
	thisPlayerNum = savedSeat;
	customWeaponNetPrepare();

	free(stream);

	// Leave the reserved port as the item data had it; nothing else in this run owns it.
	customWeaponResetAllLevels();
	customWeaponMaterialize();
}
