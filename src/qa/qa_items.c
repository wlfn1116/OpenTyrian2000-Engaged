#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "config.h"
#include "episodes.h"
#include "fnv1a.h"
#include "game_state.h"

#include "SDL.h"

#include <stdio.h>
#include <string.h>

/* Settings baked into the loaded item data do nothing on their own: something has to rewrite
 * the tables JE_loadItemDat filled, which is JE_applyItemDataSettings. */
static Uint32 qa_item_data_hash(void)
{
	const struct { const void *data; size_t size; } tables[] = {
		{ weapons,    sizeof(weapons) },
		{ options,    sizeof(options) },
		{ weaponPort, sizeof(weaponPort) },
		{ shields,    sizeof(shields) },
		{ ships,      sizeof(ships) },
		{ special,    sizeof(special) },
	};

	Uint32 hash = FNV1A32_INIT;
	for (size_t t = 0; t < COUNTOF(tables); ++t)
		hash = fnv1a32_update(hash, tables[t].data, tables[t].size);

	return hash;
}

void qa_test_item_data_settings(void)
{
	static const struct
	{
		int *intSetting;    // exactly one of the two pointers is set
		bool *boolSetting;
		int a, b;
		const char *name;
	} baked[] = {
		{ .boolSetting = &chargeLaserCannon, .a = false, .b = true, .name = "Charge-Laser" },
		{ .boolSetting = &unusedShopSprites, .a = false, .b = true, .name = "Unused Sprites" },
		{ .intSetting = &superSparkMode[SSW_MEGA_PULSE], .a = SUPER_SPARKS_OFF, .b = SUPER_SPARKS_ON,
		  .name = "Mega Pulse trail" },
		{ .intSetting = &superSparkMode[SSW_WALLOP_BEAM], .a = SUPER_SPARKS_OFF, .b = SUPER_SPARKS_ON,
		  .name = "Wallop Beam trail" },
		{ .intSetting = &superSparkMode[SSW_PROTRON_B], .a = SUPER_SPARKS_OFF, .b = SUPER_SPARKS_ON,
		  .name = "Protron -B- trail" },
		{ .intSetting = &superSparkMode[SSW_ICE], .a = SUPER_SPARKS_OFF, .b = SUPER_SPARKS_ON,
		  .name = "Ice Beam trail" },
		{ .intSetting = &wallopSecondBolt, .a = SUPER_SPARKS_OFF, .b = SUPER_SPARKS_ON,
		  .name = "Wallop 2nd Bolt" },
		{ .intSetting = &zicaLaserBase, .a = ZICA_BASE_EP13, .b = ZICA_BASE_EP4,
		  .name = "Zica Lv11 pattern" },
		{ .intSetting = &epDiffMode[EDW_XEGA_BALL], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Xega Ball" },
		{ .intSetting = &epDiffMode[EDW_MICROSOL_OPT5], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "MicroSol Opt 5" },
		{ .intSetting = &epDiffMode[EDW_FLARE], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Flare Blast" },
		{ .intSetting = &epDiffMode[EDW_NEEDLE_LASER], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Needle Laser sound" },
		{ .intSetting = &epDiffMode[EDW_BUBBLE_GUM], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Bubble Gum-Gun sound" },
		{ .intSetting = &epDiffMode[EDW_FLYING_PUNCH], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Flying Punch sound" },
		{ .intSetting = &epDiffMode[EDW_PRETZEL_MISSILE], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Pretzel Missile sound" },
		{ .intSetting = &epDiffMode[EDW_DRAGON_FROST], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Dragon Frost sound" },
		{ .intSetting = &epDiffMode[EDW_SOLAR_SHIELD], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Solar Shield icon" },
		{ .intSetting = &epDiffMode[EDW_USHIP_PIC], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "U-Ship picture" },
		{ .intSetting = &epDiffMode[EDW_NORTSHIP_PIC], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Nort Ship picture" },
	};

	for (size_t i = 0; i < COUNTOF(baked); ++i)
	{
		const int saved = baked[i].intSetting != NULL ? *baked[i].intSetting
		                                              : (*baked[i].boolSetting ? 1 : 0);

		Uint32 hash[2];
		for (int side = 0; side < 2; ++side)
		{
			const int value = side == 0 ? baked[i].a : baked[i].b;
			if (baked[i].intSetting != NULL)
				*baked[i].intSetting = value;
			else
				*baked[i].boolSetting = (value != 0);

			JE_applyItemDataSettings();
			hash[side] = qa_item_data_hash();
		}

		if (hash[0] == hash[1])
			fprintf(stderr, "# %s does not reach the item data\n", baked[i].name);
		qa_check(hash[0] != hash[1], "an item-data setting changes the item data when applied");

		if (baked[i].intSetting != NULL)
			*baked[i].intSetting = saved;
		else
			*baked[i].boolSetting = (saved != 0);
	}

	/* Reapplying the settings that were there all along has to put the tables back exactly, or
	 * a toggled row would leave the item data subtly different from a fresh load. */
	const Uint32 restored = (JE_applyItemDataSettings(), qa_item_data_hash());
	JE_applyItemDataSettings();
	qa_check(qa_item_data_hash() == restored, "reapplying the same settings is idempotent");
}

/* A gun is a port: each of its eleven power levels is a separate weapon record with its own
 * sound, and both data sets set all eleven alike. */
void qa_test_firing_sound_levels(void)
{
	static const struct
	{
		int item;
		JE_byte port;    // a gun, whose every power level has to match...
		JE_word weapon;  // ...or a sidekick's single record
		const char *name;
	} sounds[] = {
		{ EDW_NEEDLE_LASER,    .port = 43, .name = "Needle Laser" },
		{ EDW_PRETZEL_MISSILE, .port = 44, .name = "Pretzel Missile" },
		{ EDW_DRAGON_FROST,    .port = 45, .name = "Dragon Frost" },
		{ EDW_BUBBLE_GUM,    .weapon = 792, .name = "Bubble Gum-Gun" },
		{ EDW_FLYING_PUNCH,  .weapon = 794, .name = "Flying Punch" },
	};

	int savedMode[EDW_COUNT];
	memcpy(savedMode, epDiffMode, sizeof(savedMode));

	const int modes[] = { EPDIFF_EP13, EPDIFF_EP45 };
	for (size_t i = 0; i < COUNTOF(sounds); ++i)
	{
		for (size_t m = 0; m < COUNTOF(modes); ++m)
		{
			epDiffMode[sounds[i].item] = modes[m];
			JE_applyItemDataSettings();

			const JE_byte want = JE_epDiffFiringSound(sounds[i].item, modes[m]);
			qa_check(want != 0, "a firing-sound row resolves to a sound");

			int levels = 0, wrong = 0;
			if (sounds[i].port != 0)
			{
				for (unsigned int mode = 0; mode < COUNTOF(weaponPort[0].op); ++mode)
					for (unsigned int lvl = 0; lvl < COUNTOF(weaponPort[0].op[0]); ++lvl)
					{
						const JE_word wpn = weaponPort[sounds[i].port].op[mode][lvl];
						if (wpn == 0 || wpn > WEAP_NUM)
							continue;
						++levels;
						if (weapons[wpn].sound != want)
							++wrong;
					}
			}
			else
			{
				levels = 1;
				wrong = weapons[sounds[i].weapon].sound != want;
			}

			if (wrong != 0)
				fprintf(stderr, "# %s: %d of %d power levels kept the other sound\n",
				        sounds[i].name, wrong, levels);
			qa_check(levels > 0, "a firing-sound row names weapon records that exist");
			qa_check(wrong == 0, "every power level of a firing-sound row takes the chosen sound");
		}
	}

	memcpy(epDiffMode, savedMode, sizeof(savedMode));
	JE_applyItemDataSettings();
}

static bool qa_zica_pattern_is(const JE_WeaponType *lv11, const JE_shortint *sx,
                               const JE_shortint *bx)
{
	return memcmp(lv11->sx, sx, 8) == 0 && memcmp(lv11->bx, bx, 8) == 0;
}

void qa_test_zica_lv11_pattern(void)
{
	static const JE_shortint spreadSx[8]  = { -1, -1, -1, -1, 1, 1, 1, 1 };
	static const JE_shortint columnsBx[8] = { -8, -8, -8, -8, 8, 8, 8, 8 };
	static const JE_shortint flat[8]      = { 0 };

	const int wn11 = weaponPort[ZICA_LASER_PORT].op[0][ZICA_LV11_OP];
	qa_check(wn11 > 0 && wn11 <= WEAP_NUM, "the loaded item data has a Zica Laser Lv11 weapon");
	if (wn11 <= 0 || wn11 > WEAP_NUM)
		return;
	const JE_WeaponType *const lv11 = &weapons[wn11];

	const int savedBase = zicaLaserBase;
	const JE_boolean savedEndless = endlessMode;

	endlessMode = false;
	zicaLaserBase = ZICA_BASE_AUTO;
	JE_applyItemDataSettings();
	JE_shortint nativeSx[8], nativeBx[8];
	memcpy(nativeSx, lv11->sx, sizeof(nativeSx));
	memcpy(nativeBx, lv11->bx, sizeof(nativeBx));
	qa_check(qa_zica_pattern_is(lv11, spreadSx, flat) || qa_zica_pattern_is(lv11, flat, columnsBx),
	         "Auto outside Endless keeps one of the two shipped Lv11 patterns");

	zicaLaserBase = ZICA_BASE_EP13;
	JE_applyItemDataSettings();
	qa_check(qa_zica_pattern_is(lv11, flat, columnsBx), "Ep 1-3 forces the two straight columns");
	zicaLaserBase = ZICA_BASE_EP4;
	JE_applyItemDataSettings();
	qa_check(qa_zica_pattern_is(lv11, spreadSx, flat), "Ep 4-5 forces the spread");

	endlessMode = true;
	zicaLaserBase = ZICA_BASE_AUTO;
	JE_applyItemDataSettings();
	qa_check(qa_zica_pattern_is(lv11, spreadSx, flat), "Endless plays Auto as the Ep 4-5 spread");
	zicaLaserBase = ZICA_BASE_EP13;
	JE_applyItemDataSettings();
	qa_check(qa_zica_pattern_is(lv11, flat, columnsBx),
	         "...while an Ep 1-3 pick still holds in Endless");

	endlessMode = false;
	zicaLaserBase = ZICA_BASE_AUTO;
	JE_applyItemDataSettings();
	qa_check(qa_zica_pattern_is(lv11, nativeSx, nativeBx),
	         "leaving Endless puts the episode's own pattern back");

	zicaLaserBase = savedBase;
	endlessMode = savedEndless;
	JE_applyItemDataSettings();
}
