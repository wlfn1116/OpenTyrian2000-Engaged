#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "config.h"
#include "custom_weapon.h"
#include "debug_flags.h"
#include "draw.h"
#include "episodes.h"
#include "extra_ships.h"
#include "font.h"
#include "game_loop.h"
#include "game_menu.h"
#include "game_state.h"
#include "health_bars.h"
#include "hud.h"
#include "item_prices.h"
#include "network.h"
#include "player.h"
#include "player_ship.h"
#include "rollback.h"
#include "ship_editor.h"
#include "ship_skin.h"
#include "specials.h"
#include "sprite.h"
#include "video.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Arcade specifics: the purple-ball economy bounds, the link-gun weapon table, and the split
 * two-player gauge geometry against the wipe region it must stay inside. */
void qa_test_arcade_matrices(void)
{
	const Player savedPlayer = player[0];
	char label[128];

	// The purple-ball counter follows the life table for every reachable life count and the
	// power ceiling holds; a maxed gun pays cash instead of an out-of-range power step.
	Player p = player[0];
	Uint8 lives;
	p.lives = &lives;
	bool ballBounds = true;
	for (lives = 0; lives <= ARCADE_LIVES_MAX + 1; ++lives)
	{
		calc_purple_balls_needed(&p);
		ballBounds &= p.purple_balls_needed >= 1 && p.purple_balls_needed <= 50;
	}
	qa_check(ballBounds, "purple-ball pricing stays inside its table for every life count");

	lives = 5;
	p.items.weapon[FRONT_WEAPON].id = 1;
	p.items.weapon[FRONT_WEAPON].power = 11;
	p.cash = 0;
	p.is_dragonwing = false;
	p.purple_balls_needed = 1;
	handle_got_purple_ball(&p);
	qa_check(p.items.weapon[FRONT_WEAPON].power == 11 && p.cash == 1000,
	         "a purple ball at maximum power pays cash instead of a power step");
	p.purple_balls_needed = 3;
	handle_got_purple_ball(&p);
	qa_check(p.purple_balls_needed == 2 && p.cash == 1000,
	         "an unearned purple ball decrements the counter and pays nothing");

	bool linkTable = true;
	for (unsigned i = 0; i < COUNTOF(linkGunWeapons); ++i)
		linkTable &= linkGunWeapons[i] <= WEAP_NUM;
	qa_check(linkTable, "every link-gun rear weapon maps to a real weapon entry");

	// Both full split-HUD gauges must stay inside the 45-row cleared band.
	if (VGAScreen != NULL && VGAScreen->format->BytesPerPixel == 1)
	{
		for (int stride = 0; stride < 2; ++stride)
		{
			const int x = 100, y = 60 + 134 * stride;
			Uint8 *const pixels = (Uint8 *)VGAScreen->pixels;

			for (int row = y - 60; row <= y + 2; ++row)
				memset(pixels + (size_t)row * VGAScreen->pitch + x - 2, 0, 14);

			JE_dBar3(VGAScreen, x, y, 21, 144, 0, 0, 1);

			int top = -1, bottom = -1;
			for (int row = y - 60; row <= y + 2; ++row)
			{
				bool any = false;
				for (int col = x; col <= x + 8; ++col)
					any |= pixels[(size_t)row * VGAScreen->pitch + col] != 0;
				if (any)
				{
					if (top < 0)
						top = row;
					bottom = row;
				}
			}

			snprintf(label, sizeof(label),
			         "player %d split gauge paints exactly the 45-row band the wipe clears",
			         stride + 1);
			qa_check(top == y - 44 && bottom == y, label);
		}
	}

	player[0] = savedPlayer;
}

/* Every replacement icon top has to exist in its sheet and fit the space the ship half leaves. */
void qa_test_special_icon_tops(void)
{
	if (spriteSheet8.data == NULL || spriteSheet12.data == NULL)
		return;  // shape tables not loaded

	const bool saved = unusedShopSprites;
	unusedShopSprites = true;

	int replaced = 0;
	for (uint id = 1; id <= SPECIAL_NUM; ++id)
	{
		JE_word top = 0;
		const Sprite2_array *const sheet = JE_specialIconTop((JE_byte)id, &top);
		if (sheet == NULL)
			continue;
		++replaced;

		int x0, y0, x1, y1;
		char label[128];
		snprintf(label, sizeof(label), "special %u icon top fits the ship half's %dx%d",
		         id, HUD_SPECIAL_ICON_W, HUD_SPECIAL_ICON_H / 2);
		qa_check(sprite2_ink_bounds(*sheet, top, &x0, &y0, &x1, &y1)
		         && x1 - x0 < HUD_SPECIAL_ICON_W && y1 - y0 < HUD_SPECIAL_ICON_H / 2, label);
	}

	qa_check(replaced == 11, "eleven specials take a replacement icon top");
	printf("# special icon tops: %d replacements\n", replaced);

	/* Dragon Lightning has a whole spare icon to take, so it swaps itemgraphic instead of taking a
	 * rebuilt top, and only it moves: Lightning Zone keeps the icon the two shipped sharing. */
	JE_applyUnusedShopSprites();
	const JE_word onDragon = special[48].itemgraphic;
	const JE_word onZone = special[40].itemgraphic;

	unusedShopSprites = false;
	JE_applyUnusedShopSprites();
	qa_check(special[48].itemgraphic == special[40].itemgraphic,
	         "Dragon Lightning ships sharing Lightning Zone's HUD icon");
	qa_check(onDragon != onZone && onZone == special[40].itemgraphic,
	         "Unused Sprites gives Dragon Lightning its own icon and leaves Lightning Zone's");

	/* blit_sprite2x2 draws gr, gr+1, gr+19 and gr+20. sprite2_is_blank also reports an index the
	 * sheet does not hold, the out-of-range icon endlessGrantSpecial and the debug menu exclude. */
	static const unsigned int blockOffsets[4] = { 0, 1, 19, 20 };
	bool painted = true;
	for (unsigned int i = 0; i < COUNTOF(blockOffsets); ++i)
		painted = painted && !sprite2_is_blank(spriteSheet10, onDragon + blockOffsets[i]);
	qa_check(painted, "the spare icon it takes is four sprites the sheet holds and paints");

	JE_word unusedTop = 0;
	qa_check(JE_specialIconTop(41, &unusedTop) == NULL,
	         "Unused Sprites off leaves every special drawing its shipped icon");

	unusedShopSprites = saved;
	JE_applyUnusedShopSprites();
}

/* Health bars divide by each boss part's spawn armor, including script and difficulty changes. */
void qa_test_health_bar_scale(void)
{
	struct JE_SingleEnemyType part = { 0 };

	part.armorleft = 100;
	enemy_note_full_armor(&part);
	qa_check(part.healthbar_max == 100, "an undamaged enemy takes its armor as full health");

	part.armorleft = 20;  /* a script event may arm a group well below its spawn armor */
	enemy_note_full_armor(&part);
	qa_check(part.healthbar_max == 20, "a rewrite before the first wound replaces the full value");

	part.healthbar_seen = true;
	part.armorleft = 8;
	enemy_note_full_armor(&part);
	qa_check(part.healthbar_max == 20, "a wounded enemy losing armor keeps its full value");

	part.armorleft = 254;
	enemy_note_full_armor(&part);
	qa_check(part.healthbar_max == 254, "a script healing a wounded enemy raises the full value");

	part.armorleft = 255;
	enemy_note_full_armor(&part);
	qa_check(part.healthbar_max == 254, "the invincible sentinel is not a health value");

	qa_check(boss_bar_fill(254, 254) == BOSS_BAR_FULL && boss_bar_fill(100, 100) == BOSS_BAR_FULL,
	         "a boss bar starts full whatever armor its boss was given");
	qa_check(boss_bar_fill(50, 100) == 127, "half a 100-armor boss draws half a bar");
	qa_check(boss_bar_fill(1, 254) == 1, "one armor point left still draws a sliver");
	qa_check(boss_bar_fill(255, 0) == BOSS_BAR_FULL && boss_bar_fill(60, 0) == BOSS_BAR_FULL,
	         "an invincible phase and an unestablished full value both draw full");

	/* The bar reads the most-damaged part, so it has to divide by that part's own full value. */
	static const struct { JE_byte avail, armor, full; } parts[] = {
		{ 0, 200, 254 },  /* an undamaged hull */
		{ 0,  60, 100 },  /* the most-damaged part: 60 of the 100 it flew in with */
		{ 1,   1, 254 },  /* destroyed, so the survey has to skip it */
	};
	const JE_byte link = 42;
	JE_byte savedAvail[COUNTOF(parts)];
	struct JE_SingleEnemyType savedEnemy[COUNTOF(parts)];
	memcpy(savedEnemy, enemy, sizeof(savedEnemy));

	for (uint i = 0; i < COUNTOF(parts); ++i)
	{
		savedAvail[i] = enemyAvail[i];
		enemyAvail[i] = parts[i].avail;
		enemy[i].linknum = link;
		enemy[i].armorleft = parts[i].armor;
		enemy[i].healthbar_max = parts[i].full;
	}

	unsigned int armor = 0, full = 0;
	boss_bar_survey(link, &armor, &full);
	qa_check(armor == 60 && full == 100,
	         "the survey pairs the most-damaged part with its own full value");
	qa_check(boss_bar_fill(armor, full) == 152, "which fills 60% of the bar");

	/* Boss-bar surveys ignore wreckage while retaining active transformed parts. */
	enemyAvail[0] = 2;
	boss_bar_survey(link, &armor, &full);
	qa_check(armor == 60 && full == 100,
	         "a hull that leaves wreckage beside a live part still reads the live one");

	enemyAvail[1] = 2;
	boss_bar_survey(link, &armor, &full);
	qa_check(armor > 255, "wreckage a dead boss leaves behind is not a live part");

	enemyAvail[0] = 1;
	enemyAvail[1] = 1;
	boss_bar_survey(link, &armor, &full);
	qa_check(armor > 255, "a group with no live parts left reports the boss gone");

	memcpy(enemy, savedEnemy, sizeof(savedEnemy));
	for (uint i = 0; i < COUNTOF(parts); ++i)
		enemyAvail[i] = savedAvail[i];
}

/* Boss vulnerability transition and flash fade. */
void qa_test_vulnerable_cue(void)
{
	qa_check(enemy_armed_flash_arms(255, 254, 0),
	         "a level arming an invulnerable boss lights the cue");
	qa_check(enemy_armed_flash_arms(255, 1, 0), "even one armor point is a target");
	qa_check(!enemy_armed_flash_arms(100, 50, 0),
	         "an enemy that could already be hurt has nothing to announce");
	qa_check(!enemy_armed_flash_arms(255, 255, 0), "and neither has one left invulnerable");
	qa_check(!enemy_armed_flash_arms(255, 0, 0), "armor landing on zero kills rather than arms");
	qa_check(!enemy_armed_flash_arms(255, 254, 1) && !enemy_armed_flash_arms(255, 254, 2),
	         "a freed or lingering slot is no target to announce");

	qa_check(enemy_armed_flash_lift(ENEMY_ARMED_FLASH_FRAMES) == 0x0f,
	         "the body opens solid white on the frame the cue is armed");
	qa_check(enemy_armed_flash_lift(ENEMY_ARMED_FLASH_WHITE) == 0x0f,
	         "and holds white for the frames the shield gauge holds it");
	qa_check(enemy_armed_flash_lift(ENEMY_ARMED_FLASH_WHITE - 1) < 0x0f,
	         "then drops into its own shading in greys");
	qa_check(enemy_armed_flash_lift(0) == 0, "and is back in its colours once the cue is spent");

	for (Uint32 left = ENEMY_ARMED_FLASH_FRAMES; left > 0; --left)
		if (enemy_armed_flash_lift(left - 1) > enemy_armed_flash_lift(left))
		{
			qa_check(false, "the flash only ever fades");
			break;
		}
	qa_check(enemy_armed_flash_lift(1) > 0, "without going dark before the cue ends");

	/* Cue scope when one armor event affects boss and non-boss bodies. */
	const int savedCue = vulnerableCue;
	const Uint8 savedLink[2] = { boss_bar[0].link_num, boss_bar[1].link_num };
	boss_bar[0].link_num = 42;
	boss_bar[1].link_num = 0;

	vulnerableCue = VULN_CUE_BOSSES;
	qa_check(enemy_armed_flash_shows(42), "a hull carrying a boss bar shows the cue");
	qa_check(!enemy_armed_flash_shows(7), "a linked hull with no bar of its own does not");
	qa_check(!enemy_armed_flash_shows(0), "and neither does unlinked traffic");

	vulnerableCue = VULN_CUE_ALL;
	qa_check(enemy_armed_flash_shows(0) && enemy_armed_flash_shows(7),
	         "All Enemies hands the cue to everything an armor event arms");

	vulnerableCue = VULN_CUE_OFF;
	qa_check(!enemy_armed_flash_shows(42), "Off silences it even on a boss");

	vulnerableCue = savedCue;
	boss_bar[0].link_num = savedLink[0];
	boss_bar[1].link_num = savedLink[1];
}

/* Item data points one of the Flying Punch's bolts (weapon 794, `sg[0]`) at People Pretzels'
 * sprite, so the load pass redirects it. Every frame that bolt draws has to be blank. */
void qa_test_flying_punch_bolt(void)
{
	const JE_word sg = weapons[794].sg[0];
	if (spriteSheet12.data == NULL || sg == 0)
		return;  // shape tables or item data not loaded

	qa_check(sg > 500 && sg <= 1000, "the Flying Punch's center bolt draws from spriteSheet12");

	for (JE_word ani = 0; ani <= weapons[794].weapani; ++ani)
	{
		int x0, y0, x1, y1;
		char label[96];
		snprintf(label, sizeof(label), "Flying Punch center bolt frame %u paints nothing", ani + 1u);
		qa_check(!sprite2_ink_bounds(spriteSheet12, sg - 500 + ani, &x0, &y0, &x1, &y1), label);
	}
}

/* Check the synthesized Dragonwing row, including its sentinel graphic, stats, and id clamps. */
void qa_test_dragonwing_row(void)
{
	const JE_byte hull = ships[SHIP_DRAGONWING].dmg;
	const JE_word cost = ships[SHIP_DRAGONWING].cost;

	qa_check(ships[SHIP_DRAGONWING].name[0] != '\0' && ships[SHIP_DRAGONWING].shipgraphic == 0,
	         "the Dragonwing row exists and keeps the two-piece hull sentinel");

	qa_check(hull > ships[4].dmg && hull < ships[5].dmg
	         && cost > ships[4].cost && cost < ships[5].cost,
	         "the Dragonwing's hull and price sit between the Gencores and the Stalkers");

	// Earlier tests may leave a price or hull scaling mode on; these want the raw table values.
	const JE_boolean savedEndless = endlessMode, savedExpert = expertMode;
	const JE_boolean savedTwo = twoPlayerMode, savedSeparate = arcadeSeparateMode;
	const bool savedCoop = coopCampaignMode, savedCoopEndless = coopEndlessMode;
	endlessMode = false;
	expertMode = false;

	qa_check(JE_getCost(2, SHIP_DRAGONWING) == cost,
	         "the shop prices the Dragonwing row, not the out-of-table fallback");

	const Player saved0 = player[0], saved1 = player[1];
	const JE_word savedGr = shipGr, savedGr2 = shipGr2;
	const uint savedPowerAdd = powerAdd;
	Sprite2_array *const savedGrPtr = shipGrPtr, *const savedGr2Ptr = shipGr2ptr;

	player[0].items.ship = SHIP_DRAGONWING;
	JE_getShipInfo();
	qa_check(player[0].armor == hull && shipGr == 0 && shipGrPtr == &spriteSheet9,
	         "a seat flying the bought Dragonwing gets its hull and the sentinel graphic");

	/* Graphic 0 means the linked rear bay only outside a two-ship session. */
	twoPlayerMode = true;
	coopCampaignMode = true;
	coopEndlessMode = false;
	player[1].items.ship = SHIP_DRAGONWING;
	JE_getShipInfo();
	qa_check(shipGr2 == 0 && player[1].hull_armor == hull,
	         "a co-op seat two flying the Dragonwing keeps its own hull, not the linked bay's");

	// Linked Arcade identifies seat two by sentinel; items.ship is empty.
	coopCampaignMode = false;
	arcadeSeparateMode = false;
	player[1].items.ship = 0;
	JE_getShipInfo();
	qa_check(split_arcade_mode() && shipGr2 == 0 && shipGr2ptr == &spriteSheet9,
	         "the linked pair names seat two's Dragonwing by sentinel, with no ship item to read");

	shipGrPtr = savedGrPtr;
	shipGr2ptr = savedGr2Ptr;
	shipGr = savedGr;
	shipGr2 = savedGr2;
	powerAdd = savedPowerAdd;
	player[0] = saved0;
	player[1] = saved1;
	coopEndlessMode = savedCoopEndless;
	coopCampaignMode = savedCoop;
	arcadeSeparateMode = savedSeparate;
	twoPlayerMode = savedTwo;
	expertMode = savedExpert;
	endlessMode = savedEndless;
}

/* Painted x-extent of one 2x2 hull piece (cells at +0 and +12 within it) shifted by xOff, folded
 * into an accumulating min/max that `painted` marks as already holding a value. */
static bool qa_fold_2x2_ink(Sprite2_array sheet, unsigned int index, int xOff, bool painted,
                            int *left, int *right)
{
	static const struct { unsigned int add; int dx; } cells[] = {
		{0, 0}, {1, 12}, {19, 0}, {20, 12},
	};

	for (uint c = 0; c < COUNTOF(cells); ++c)
	{
		int x0, y0, x1, y1;
		if (!sprite2_ink_bounds(sheet, index + cells[c].add, &x0, &y0, &x1, &y1))
			continue;

		const int l = xOff + cells[c].dx + x0, r = xOff + cells[c].dx + x1;
		if (!painted || l < *left)
			*left = l;
		if (!painted || r > *right)
			*right = r;
		painted = true;
	}
	return painted;
}

/* The Nort Ship and Dragonwing paint 48px wide against the item list's 24px icon column, so
 * each takes a shifted anchor and a label column of its own. */
void qa_test_wide_hull_columns(void)
{
	if (spriteSheet9.data == NULL)
		return;  // shape tables not loaded

	// The tightest clearance a normal hull leaves is the gap the wide ones have to match.
	int gap = -1;
	for (uint ship = 1; ship <= SHIP_NUM; ++ship)
	{
		const JE_word gr = ships[ship].shipgraphic;
		if (gr <= 1)
			continue;  // the two-piece hulls, measured below

		const bool t2000 = gr > 500;
		int left = 0, right = 0;
		if (!qa_fold_2x2_ink(t2000 ? spriteSheetT2000 : spriteSheet9, t2000 ? gr - 500 : gr,
		                     0, false, &left, &right))
			continue;

		const int shipGap = SHOP_ITEM_NAME_X - (SHOP_ITEM_ICON_X + right) - 1;
		if (gap < 0 || shipGap < gap)
			gap = shipGap;
	}
	qa_check(gap >= 0, "the item list's normal ship hulls clear its label column");
	if (gap < 0)
		return;

	// Left and right half indices JE_drawItem blits, by ship.
	static const struct { const char *name; JE_word ship; unsigned int left, right; } hulls[] = {
		{ "Dragonwing", SHIP_DRAGONWING, 13, 51 },
		{ "Nort Ship",  12,             220, 222 },
	};

	for (uint i = 0; i < COUNTOF(hulls); ++i)
	{
		char label[128];
		int inkLeft = 0, inkRight = 0;
		bool painted = qa_fold_2x2_ink(spriteSheet9, hulls[i].left, 0, false, &inkLeft, &inkRight);
		painted = qa_fold_2x2_ink(spriteSheet9, hulls[i].right, 24, painted, &inkLeft, &inkRight);
		if (!painted)
		{
			snprintf(label, sizeof(label), "the %s hull paints something", hulls[i].name);
			qa_check(false, label);
			continue;
		}

		// JE_drawItem straddles its anchor, so the left half lands a half-width before it.
		const ShopItemColumns cols = shop_ship_item_columns(hulls[i].ship);
		const int boxLeft = cols.iconX - SHOP_WIDE_HULL_HALF;

		snprintf(label, sizeof(label), "the %s hull starts at the icon column", hulls[i].name);
		qa_check(boxLeft == SHOP_ITEM_ICON_X && boxLeft + inkLeft >= SHOP_ITEM_ICON_X, label);

		snprintf(label, sizeof(label), "the %s label clears its own hull", hulls[i].name);
		qa_check(cols.nameX == boxLeft + inkRight + 1 + gap, label);

		snprintf(label, sizeof(label), "the %s cost row keeps the label offset", hulls[i].name);
		qa_check(cols.costX - cols.nameX == SHOP_ITEM_COST_X - SHOP_ITEM_NAME_X, label);

		printf("# %s hull: ink %d..%d, label x %d\n", hulls[i].name, inkLeft, inkRight, cols.nameX);
	}
	printf("# wide shop hulls clear their labels by %dpx\n", gap);
}

/* A weapon row tags a two-mode port, or a gun's issuing bay in Endless, after its cost, in a
 * column that has to clear both the cost text and the owned marker the same row can carry. */
void qa_test_dual_mode_tag(void)
{
	const int tagW = JE_textWidth(SHOP_DUAL_MODE_TAG, TINY_FONT);
	if (tagW <= 0)
		return;  // font bank not loaded

	qa_check(JE_textWidth(SHOP_FRONT_GUN_TAG, TINY_FONT) <= tagW
	         && JE_textWidth(SHOP_REAR_GUN_TAG, TINY_FONT) <= tagW,
	         "the bay tags fit the column the Dual-Mode tag sizes");

	// The ports the tag marks, and the priciest of them.
	uint dual = 0;
	JE_word base = 0;
	for (uint port = 1; port <= PORT_NUM; ++port)
	{
		if (weaponPort[port].opnum != 2)
			continue;
		++dual;
		base = MAX(base, weaponPort[port].cost);
	}
	qa_check(dual > 0 && base > 0, "the item data has two-mode ports for the rear list to tag");
	if (dual == 0)
		return;

	// Multipliers JE_getCost can stack on a shipped price: Endless depth caps at 100x and the
	// expert Shop Cost knob at 20x.
	static const struct { const char *what; ulong mult; } prices[] = {
		{ "campaign",       1 },
		{ "deep Endless",   100 },
		{ "the price cap",  100 * 20 },
	};

	for (uint i = 0; i < COUNTOF(prices); ++i)
	{
		char buf[32], label[128];
		snprintf(buf, sizeof(buf), "Cost: %lld", (long long)base * (long long)prices[i].mult);

		// The row that carries the marker is the tighter case, so measure against that column.
		const int markerX = SHOP_ITEM_MARKER_X(true);
		const int costRight = SHOP_ITEM_COST_X + JE_textWidth(buf, TINY_FONT);
		const int tagX = shop_row_tag_x(costRight, tagW, markerX);

		snprintf(label, sizeof(label), "the Dual-Mode tag clears a %s price", prices[i].what);
		qa_check(tagX >= costRight, label);

		snprintf(label, sizeof(label), "the Dual-Mode tag stays in the row at a %s price",
		         prices[i].what);
		qa_check(tagX + tagW <= SHOP_ITEM_LIST_RIGHT, label);

		printf("# Dual-Mode tag, %s price \"%s\": cost ends %d, tag %d..%d\n",
		       prices[i].what, buf, costRight, tagX, tagX + tagW);
	}
	printf("# Dual-Mode tag: %u two-mode ports, %dpx wide, column ends %d\n",
	       dual, tagW, SHOP_ITEM_MARKER_X(true) - SHOP_ROW_TAG_MARKER_GAP);

	// The same row can carry the owned marker, whose icon paints from the marker column.
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // the shop loads this bank lazily

	int inkX0, inkY0, inkX1, inkY1;
	if (sprite2_ink_bounds(shopSpriteSheet, SHOP_OWNED_MARKER_SPRITE,
	                       &inkX0, &inkY0, &inkX1, &inkY1))
	{
		const int markerX = SHOP_ITEM_MARKER_X(true);
		const int tagRight = markerX - SHOP_ROW_TAG_MARKER_GAP;

		qa_check(tagRight < markerX + inkX0, "a row tag clears the owned marker icon");
		printf("# owned marker: icon ink %d..%d, tag column ends %d\n",
		       markerX + inkX0, markerX + inkX1, tagRight);
	}
}

// Tags are compared by text: each translation unit gets its own copy of the macro's literal.
static bool qa_tag_is(const char *tag, const char *want)
{
	if (tag == NULL || want == NULL)
		return tag == want;
	return strcmp(tag, want) == 0;
}

/* Endless gun rows carry one issuing bay; campaign shop rows carry none. */
void qa_test_weapon_bay_tags(void)
{
	uint front = 0, rear = 0, unclassified = 0;

	for (uint port = 1; port <= SHOP_REAL_WEAPON_PORTS; ++port)
	{
		char label[96];
		const ShopWeaponBay bay = shop_weapon_port_bay(port);
		const bool dualMode = weaponPort[port].opnum == 2;

		switch (bay)
		{
		case SHOP_BAY_FRONT:
			++front;
			break;
		case SHOP_BAY_REAR:
			++rear;
			break;
		default:
			++unclassified;
			break;
		}

		// A two-mode port has to be a rear gun, or its tag and a bay tag want the same column.
		snprintf(label, sizeof(label), "port %u (%.16s) has a second mode only as a rear gun",
		         port, weaponPort[port].name);
		qa_check(!dualMode || bay == SHOP_BAY_REAR, label);

		const char *const wantBay = bay == SHOP_BAY_FRONT ? SHOP_FRONT_GUN_TAG
		                          : bay == SHOP_BAY_REAR  ? SHOP_REAR_GUN_TAG : NULL;
		const char *const wantRear = dualMode ? SHOP_DUAL_MODE_TAG : wantBay;

		snprintf(label, sizeof(label), "port %u names its bay in a mixed list", port);
		qa_check(qa_tag_is(shop_weapon_row_tag(port, false, true), wantBay)
		         && qa_tag_is(shop_weapon_row_tag(port, true, true), wantRear), label);

		snprintf(label, sizeof(label), "port %u carries no bay tag in a campaign shop", port);
		qa_check(qa_tag_is(shop_weapon_row_tag(port, false, false), NULL)
		         && qa_tag_is(shop_weapon_row_tag(port, true, false),
		                      dualMode ? SHOP_DUAL_MODE_TAG : NULL), label);
	}

	// Port 16 holds the sidekick weapon table; every other real port belongs to a bay.
	qa_check(front > 0 && rear > 0 && unclassified == 1, "the bay table covers the weapon ports");

	// Help text draws at x=10 and DARKEN shades one pixel right; the bar is the hard limit.
	// A line reaching the ping band only hides that row's online ping (save_help_bar_ping_band).
	qa_check(10 + JE_textWidth(SHOP_PRIMARY_GUN_HELP, TINY_FONT) + 1 <= LEGACY_WIDTH
	         && 10 + JE_textWidth(SHOP_SECONDARY_GUN_HELP, TINY_FONT) + 1 <= LEGACY_WIDTH,
	         "the bay help lines stay on the help bar");
	printf("# weapon bays: %u front, %u rear, %u unclassified of %d ports; tags %dpx / %dpx\n",
	       front, rear, unclassified, SHOP_REAL_WEAPON_PORTS,
	       JE_textWidth(SHOP_FRONT_GUN_TAG, TINY_FONT), JE_textWidth(SHOP_REAR_GUN_TAG, TINY_FONT));
}

// Check the editor codec against Tyrian 2000's stock newsh$.shp.
void qa_test_ship_editor_file(void)
{
	JE_ShipsType backup, enc;
	memcpy(backup, extraShips, sizeof(backup));
	qa_check(strstr(ships[1].name, "Light Fighter") != NULL,
	         "the USP Talon keeps its full data-file name outside the ship editor");
	char shortName[32];
	qa_check(strcmp(extraShipEditorGraphicName(1, shortName, sizeof(shortName)), "USP Talon") == 0,
	         "the ship editor shortens the USP Talon's graphic label");

	qa_check(extraAvail, "the stock compiled ship file loads");
	if (extraAvail)
	{
		qa_check(extraShips[0] == 8 && extraShips[1] == 6 && extraShips[7] == 16 && extraShips[8] == 5,
		         "slot one decodes to its known loadout");

		bool bays = true;
		for (int slot = 0; slot < 10; ++slot)
		{
			const JE_byte *record = &extraShips[slot * 15];
			bays = bays && shop_weapon_port_bay(record[1]) == SHOP_BAY_FRONT;
			bays = bays && (record[2] == 0 || shop_weapon_port_bay(record[2]) == SHOP_BAY_REAR);
		}
		qa_check(bays, "stock loadouts fit the editor's front/rear bay split");
	}

	JE_encryptShips(enc);
	memcpy(extraShips, enc, sizeof(extraShips));
	qa_check(JE_decryptShips(), "an encrypted table passes its own checksums");
	qa_check(memcmp(extraShips, backup, sizeof(JE_ShipsType) - 4) == 0,
	         "encrypt then decrypt returns the original table");

	memcpy(extraShips, enc, sizeof(extraShips));
	extraShips[5] ^= 0x40;
	qa_check(!JE_decryptShips(), "a corrupted table is rejected");

	memcpy(extraShips, backup, sizeof(backup));

	qa_check(JE_shapeCodecSelfTest(), "the sprite cell codec round-trips every cell");
	qa_check(JE_legacyUserShapeSelfTest(),
	         "a legacy User.shp imports every cell and becomes transferable custom ships");
	qa_check(JE_legacyUserShapeCaseSelfTest(),
	         "User.shp discovery is case-insensitive on case-sensitive filesystems");
	qa_check(JE_stockExtraShapesSelfTest(),
	         "the ship editor can reload the untouched stock newsh$.shp");
	qa_check(JE_shipEditorConfirmationSelfTest(),
	         "destructive ship-editor actions require two consecutive activations");
	qa_check(JE_captureHullListSelfTest(),
	         "the sprite editor offers each compatible hull graphic exactly once");
	qa_check(JE_shipEditorGraphicCycleSelfTest(),
	         "the ship editor cycles Nort Ship immediately after Dragonwing");
	qa_check(JE_shipStripImageSelfTest(),
	         "a custom bank's pose strip round-trips through indexed, RGB and alpha images");
	{
		const unsigned int gr = ships[16].shipgraphic - 500;
		qa_check(ships[16].shipgraphic == 581 &&
		         sprite2_hflip_equal(spriteSheetT2000, gr - 4, gr + 5) &&
		         sprite2_hflip_equal(spriteSheetT2000, gr - 3, gr + 4) &&
		         sprite2_hflip_equal(spriteSheetT2000, gr + 15, gr + 24) &&
		         sprite2_hflip_equal(spriteSheetT2000, gr + 16, gr + 23),
		         "Gencore II replaces its malformed hard-left pose with a clean mirror");
	}
	{
		static const struct { int pose, x, y; } hole[9] = {
			{ -2, 11, 1 }, { -2, 12, 2 }, { -2, 20, 10 }, { -2, 19, 11 },
			{ 0, 13, 2 },
			{ 2, 12, 1 }, { 2, 11, 2 }, { 2, 3, 10 }, { 2, 4, 11 },
		};
		const unsigned int gr = ships[16].shipgraphic - 500;
		bool white = ships[16].shipgraphic == 581;
		for (uint i = 0; i < COUNTOF(hole); ++i)
		{
			const unsigned int pose = gr + hole[i].pose;
			white = white && sprite2x2_pixel(spriteSheetT2000, pose, hole[i].x, hole[i].y) == 0x0f;
		}
		qa_check(white, "Gencore II fills the highlight holes in its center and slight-bank poses");
	}
	{
		bool same = true;
		for (unsigned int gr = 1; gr <= 300 && same; gr += 19)
		{
			Uint8 pixels[2 * SPRITE2_CELL_H][2 * SPRITE2_CELL_W];
			sprite2x2_decode(spriteSheet9, gr, pixels);
			for (int y = 0; y < 2 * SPRITE2_CELL_H; ++y)
				for (int x = 0; x < 2 * SPRITE2_CELL_W; ++x)
					same = same && pixels[y][x] == sprite2x2_pixel(spriteSheet9, gr, x, y);
		}
		qa_check(same, "a decoded 2x2 hull matches sprite2x2_pixel at every pixel");
	}
	{
		const unsigned int gr = ships[10].shipgraphic;
		qa_check(gr == 271 &&
		         !sprite2_has_color(spriteSheet9, gr + 5, 0xfe) &&
		         !sprite2_has_color(spriteSheet9, gr + 18, 0xfe) &&
		         !sprite2_has_color(spriteSheet9, gr + 22, 0xfe) &&
		         !sprite2_has_color(spriteSheet9, gr + 24, 0xfe),
		         "the stock U-Ship poses contain no stray white pixels");
	}

	{
		static const JE_word legacy[] = { 233, 157, 195, 271, 81, 0, 119 };
		static const JE_word custom[] = { 5, 43, 81, 119, 157, 195, 233, 271 };
		Sprite2_array *seenSheets[SHIP_DRAGONWING + 7];
		JE_word seenGraphics[SHIP_DRAGONWING + 7];
		const JE_byte savedGraphic = extraShips[0];
		const bool savedNet = isNetworkGame;
		const int maxGraphic = extraShipGraphicMax();
		int seenCount = 0;
		bool unique = true, named = true, nort = false, dragonwing = false, t2000 = false;
		bool legacyCompatible = true, customCompatible = true;

		isNetworkGame = false;
		for (int graphic = 1; graphic <= maxGraphic; ++graphic)
		{
			extraShips[0] = (JE_byte)graphic;
			Sprite2_array *sheet = NULL;
			const JE_word gr = JE_SGr(0, 1, &sheet);
			if (extraShipGraphicIsCustom(graphic))
				continue;

			for (int i = 0; i < seenCount; ++i)
				unique = unique && !(seenSheets[i] == sheet && seenGraphics[i] == gr);
			seenSheets[seenCount] = sheet;
			seenGraphics[seenCount++] = gr;
			named = named &&
			        extraShipEditorGraphicName(graphic, shortName, sizeof(shortName)) != NULL;
			nort = nort || (sheet == &spriteSheet9 && gr == 1);
			dragonwing = dragonwing || (sheet == &spriteSheet9 && gr == 0);
			t2000 = t2000 || sheet == &spriteSheetT2000;
		}

		for (uint i = 0; i < COUNTOF(legacy); ++i)
		{
			extraShips[0] = (JE_byte)(i + 1);
			Sprite2_array *sheet = NULL;
			legacyCompatible = legacyCompatible && JE_SGr(0, 1, &sheet) == legacy[i]
			                   && sheet == &spriteSheet9;
		}
		for (uint i = 0; i < COUNTOF(custom); ++i)
		{
			extraShips[0] = (JE_byte)(i + 8);
			Sprite2_array *sheet = NULL;
			customCompatible = customCompatible && JE_SGr(0, 1, &sheet) == custom[i]
			                   && sheet == &extraShapes;
		}

		extraShips[0] = savedGraphic;
		isNetworkGame = savedNet;
		qa_check(maxGraphic > 15 && maxGraphic <= 255,
		         "the loadout graphic row extends beyond the original ShipEdit choices");
		qa_check(unique, "the loadout graphic row contains no duplicate built-in sprite identity");
		qa_check(named, "every built-in graphic choice has an in-game ship name");
		qa_check(nort && dragonwing, "the loadout graphic row includes Nort Ship and Dragonwing");
		qa_check(t2000, "the loadout graphic row includes Tyrian 2000 hull art");
		qa_check(legacyCompatible && customCompatible,
		         "graphics 1 through 15 retain their original ShipEdit meanings");
		printf("# ship editor graphics: %d unique built-ins, 8 custom banks, codes 1..%d\n",
		       seenCount, maxGraphic);
	}

	{
		const bool savedEnabled = customWeaponEnabled;
		const int savedPorts[CUSTOM_WEAPON_OWNERS] = { customWeaponOwnerPort[0], customWeaponOwnerPort[1] };

		qa_check(extraShipResolvePort(0, 12) == 12, "an ordinary weapon byte resolves to itself");

		customWeaponEnabled = true;
		customWeaponOwnerPort[0] = 60;
		customWeaponOwnerPort[1] = 59;
		qa_check(extraShipResolvePort(0, EXTRA_SHIP_CUSTOM_PORT) == 60 &&
		         extraShipResolvePort(1, EXTRA_SHIP_CUSTOM_PORT) == 59,
		         "each seat resolves the sentinel to its own reserved port");

		customWeaponEnabled = false;
		qa_check(extraShipResolvePort(0, EXTRA_SHIP_CUSTOM_PORT) == 60 &&
		         extraShipResolvePort(1, EXTRA_SHIP_CUSTOM_PORT) == 59,
		         "the sentinel resolves the same with the local toggle off");

		customWeaponEnabled = true;
		customWeaponOwnerPort[0] = 0;
		qa_check(extraShipResolvePort(0, EXTRA_SHIP_CUSTOM_PORT) == 0,
		         "...and where no port was free for that seat");

		customWeaponOwnerPort[0] = savedPorts[0];
		customWeaponOwnerPort[1] = savedPorts[1];
		customWeaponEnabled = savedEnabled;
	}

	{
		Uint8 *const stream = malloc(EXTRA_SHIPS_WIRE_MAX);
		const size_t total = extraShipsSerialize(stream, EXTRA_SHIPS_WIRE_MAX);
		qa_check(total >= 6 + sizeof(JE_ShipsType), "the ship file serializes for the wire");
		qa_check(extraShipsAdopt(1, stream, total), "a serialized ship file adopts into a seat");
		qa_check(!extraShipsAdopt(1, stream, 10), "a truncated ship file is refused");

		const bool savedNet = isNetworkGame;
		isNetworkGame = true;
		qa_check(extraShipsFor(1) != extraShips &&
		         memcmp(extraShipsFor(1), extraShips, sizeof(JE_ShipsType)) == 0 &&
		         extraAvailFor(1) == extraAvail &&
		         extraShapesFor(1)->size == extraShapes.size,
		         "online seat accessors serve the adopted copy");
		isNetworkGame = false;
		qa_check(extraShipsFor(1) == extraShips, "offline both seats read the local file");
		isNetworkGame = savedNet;

		extraShipsNetReset();
		free(stream);
	}
}

static JE_byte qa_apply_skin(JE_word gr, Sprite2_array **sheet, JE_word *look)
{
	*sheet = &spriteSheet9;
	*look = gr;
	return shipSkinApply(0, sheet, look);
}

void qa_test_ship_skins(void)
{
	enum { CELLS = 304 };

	const JE_byte savedSkin[2] = { shipSkinGet(0), shipSkinGet(1) };
	const JE_word savedGr = shipGr;
	const Sprite2_array savedShapes = extraShapes;
	const bool savedAvail = extraAvail, savedNet = isNetworkGame;
	const JE_boolean savedTwo = twoPlayerMode, savedTimed = timedBattleMode;

	isNetworkGame = false;
	twoPlayerMode = false;
	timedBattleMode = false;

	int wide = 0;
	bool wideRefused = true;
	for (int graphic = 1; graphic <= extraShipGraphicMax(); ++graphic)
		if (extraShipGraphicIsWide(graphic))
		{
			++wide;
			wideRefused = wideRefused && !shipSkinOffered(0, graphic);
		}
	qa_check(wide == 2 && wideRefused,
	         "the two-piece Dragonwing and Nort Ship are never offered as skins");

	union
	{
		Uint16 align;
		Uint8 bytes[CELLS * 2 + 4];
	} sheet;
	const Uint16 blank = CELLS * 2, ink = CELLS * 2 + 1;
	for (int i = 0; i < CELLS; ++i)
		((Uint16 *)sheet.bytes)[i] = SDL_SwapLE16(blank);
	sheet.bytes[blank] = 0x0f;
	sheet.bytes[ink] = 0x10;
	sheet.bytes[ink + 1] = 0x9c;
	sheet.bytes[ink + 2] = 0x0f;
	extraShapes = (Sprite2_array){ sizeof(sheet.bytes), sheet.bytes };
	extraAvail = true;

	Sprite2_array *custom = NULL;
	const JE_word inked = extraShipGraphicSprite(0, 9, &custom);
	((Uint16 *)sheet.bytes)[inked - 1] = SDL_SwapLE16(ink);
	qa_check(custom == &extraShapes && !shipSkinOffered(0, 8) && shipSkinOffered(0, 9),
	         "a custom bank is offered as a skin only once it has artwork");
	extraAvail = false;
	qa_check(!shipSkinOffered(0, 9), "custom bank skins need the seat's ship file");
	extraAvail = true;

	int order[SHIP_SKIN_ORDER_MAX] = { 0 };
	const int count = shipSkinOrder(0, order, COUNTOF(order));
	bool grouped = count > 2 && order[0] == SHIP_SKIN_NONE;
	bool customSeen = false;
	int customCount = 0;
	for (int i = 1; i < count; ++i)
	{
		const bool isCustom = extraShipGraphicIsCustom(order[i]);
		grouped = grouped && (isCustom || !customSeen) && shipSkinOffered(0, order[i]);
		customSeen = customSeen || isCustom;
		customCount += isCustom ? 1 : 0;
	}
	qa_check(grouped && customCount == 1 && order[count - 1] == 9,
	         "the skin cycle runs Default, then in-game hulls, then custom banks with artwork");

	shipSkinSet(0, (JE_byte)order[1]);
	Sprite2_array *expectSheet = NULL;
	const JE_word expectGr = extraShipGraphicSprite(0, order[1], &expectSheet);
	Sprite2_array *look;
	JE_word lookGr;
	qa_check(qa_apply_skin(233, &look, &lookGr) == order[1] && lookGr == expectGr &&
	         look == expectSheet,
	         "a single-piece hull draws the chosen skin's sprite");

	qa_check(qa_apply_skin(0, &look, &lookGr) == SHIP_SKIN_NONE && lookGr == 0 &&
	         qa_apply_skin(1, &look, &lookGr) == SHIP_SKIN_NONE && lookGr == 1 &&
	         look == &spriteSheet9,
	         "the two-piece sentinels keep their own hulls");

	timedBattleMode = true;
	qa_check(qa_apply_skin(233, &look, &lookGr) == SHIP_SKIN_NONE && lookGr == 233 &&
	         look == &spriteSheet9,
	         "modes without custom ships draw the real hull");
	timedBattleMode = false;

	shipSkinSet(0, 250);
	qa_check(shipSkinEffective(0) == SHIP_SKIN_NONE &&
	         qa_apply_skin(233, &look, &lookGr) == SHIP_SKIN_NONE && lookGr == 233,
	         "a stored skin the data no longer offers draws the real hull");

	shipGr = 0;
	const bool wideWearer = shipSkinWearable(0);
	shipGr = 233;
	qa_check(!wideWearer && shipSkinWearable(0) && !shipSkinWearable(2),
	         "only a single-piece hull in a custom-ship mode can wear a skin");

	char name[32];
	qa_check(strcmp(shipSkinName(SHIP_SKIN_NONE, name, sizeof(name)), "Default") == 0 &&
	         strcmp(shipSkinName(9, name, sizeof(name)), "Custom Bank 2") == 0,
	         "skin names match the ship editor's graphic labels");
	qa_check(game_menu_test_skin_row_name(),
	         "a skinned ship's shop row shows its short name plus (Skin) before the marker");

	shipSkinSet(1, 7);
	shipSkinReset();
	qa_check(shipSkinGet(0) == SHIP_SKIN_NONE && shipSkinGet(1) == SHIP_SKIN_NONE,
	         "a reset clears both seats' skins");

	extraShapes = savedShapes;
	extraAvail = savedAvail;
	shipGr = savedGr;
	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	timedBattleMode = savedTimed;
	shipSkinSet(0, savedSkin[0]);
	shipSkinSet(1, savedSkin[1]);
}

/* Shield and armor glows are personal presentation state. They advance once per real tick,
 * outside rollback re-simulation. */
void qa_test_gauge_flash_lifetime(void)
{
	if (VGAScreenSeg == NULL)
		return;  // the repaint below paints the real HUD surface

	const bool savedResim = rollback_resim, savedDirty = hud_bars_dirty;
	const int savedShield[2] = { shieldGaugeFlash[0], shieldGaugeFlash[1] };
	const int savedArmor[2]  = { armorGaugeFlash[0],  armorGaugeFlash[1]  };

	shieldGaugeFlash[0] = shieldGaugeFlash[1] = 6;
	armorGaugeFlash[0]  = armorGaugeFlash[1]  = 0;

	rollback_resim = true;
	for (int i = 0; i < 4; ++i)
		JE_updateGaugeFlash();
	qa_check(shieldGaugeFlash[0] == 6 && shieldGaugeFlash[1] == 6,
	         "a rollback replay pass never spends a gauge flash");

	rollback_resim = false;
	JE_updateGaugeFlash();
	qa_check(shieldGaugeFlash[0] == 5 && shieldGaugeFlash[1] == 5,
	         "a live tick steps every ship's gauge flash exactly once");

	// Both ships hit at once, then only one of them: neither run touches the other's counter.
	shieldGaugeFlash[0] = 6;
	shieldGaugeFlash[1] = 0;
	JE_updateGaugeFlash();
	qa_check(shieldGaugeFlash[0] == 5 && shieldGaugeFlash[1] == 0,
	         "one ship's damage flash neither starts nor shortens the other's");

	rollback_resim = savedResim;
	shieldGaugeFlash[0] = savedShield[0]; shieldGaugeFlash[1] = savedShield[1];
	armorGaugeFlash[0]  = savedArmor[0];  armorGaugeFlash[1]  = savedArmor[1];
	hud_bars_dirty = savedDirty;
}

/* The two repair specials have to stay distinct where there are two hulls. */
void qa_test_partner_repair_special(void)
{
	const bool savedTwo = twoPlayerMode, savedCoop = coopCampaignMode, savedSep = arcadeSeparateMode;
	const Player saved0 = player[0], saved1 = player[1];
	const JE_byte savedStype = special[SPECIAL_NUM].stype, savedTemp2 = temp2;

	temp2 = 0;                      // heal is temp2 / 4 + 1, so one point
	special[SPECIAL_NUM].stype = 14;
	twoPlayerMode = true; coopCampaignMode = true; arcadeSeparateMode = false;

	for (uint p = 0; p < 2; ++p)
	{
		player[p].armor = 10;
		player[p].initial_armor = 50;
	}
	JE_specialComplete(1, SPECIAL_NUM);
	qa_check(player[1].armor == 11 && player[0].armor == 10,
	         "co-op: the partner-repair special mends the OTHER ship, not the firer");
	player[0].armor = player[1].armor = 10;   // reset, so the second seat is its own check
	JE_specialComplete(2, SPECIAL_NUM);
	qa_check(player[0].armor == 11 && player[1].armor == 10,
	         "...and mends ship one when ship two fires it");

	// stype 13 is the self-repair, and must not have become the same special.
	special[SPECIAL_NUM].stype = 13;
	player[0].armor = player[1].armor = 10;
	JE_specialComplete(1, SPECIAL_NUM);
	qa_check(player[0].armor == 11 && player[1].armor == 10,
	         "co-op: the self-repair special still mends the firer");

	// The linked pair shares one arsenal and has no partner ship, so hull two keeps it.
	coopCampaignMode = false;
	special[SPECIAL_NUM].stype = 14;
	player[0].armor = player[1].armor = 10;
	JE_specialComplete(1, SPECIAL_NUM);
	qa_check(player[1].armor == 11 && player[0].armor == 10,
	         "linked arcade: the partner-repair special stays on hull two");

	special[SPECIAL_NUM].stype = savedStype; temp2 = savedTemp2;
	twoPlayerMode = savedTwo; coopCampaignMode = savedCoop; arcadeSeparateMode = savedSep;
	player[0] = saved0; player[1] = saved1;
}
