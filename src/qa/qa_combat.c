#define ENDLESS_INTERNAL_ACCESS
#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "config.h"
#include "debug_flags.h"
#include "endless.h"
#include "enemy_death.h"
#include "episodes.h"
#include "endless_internal.h"
#include "explosions.h"
#include "game_loop.h"
#include "game_state.h"
#include "health_bars.h"
#include "hud.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "mtrand.h"
#include "net_style.h"
#include "network.h"
#include "player.h"
#include "player_ship.h"
#include "rollback.h"
#include "shots.h"
#include "sparks.h"
#include "sprite.h"
#include "video.h"
#include "vt_ship.h"

#include "SDL.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A 12-pixel test cell with two four-pixel rows exercises the packed sprite controls. */
static const Uint8 qa_cell_body[] = {
	0x0c,                                // row 0: 12 transparent columns, then the next row
	0x42, 0x11, 0x11, 0x11, 0x11, 0x06,  // row 1: skip 2, four pixels, skip the rest
	0x42, 0x11, 0x11, 0x11, 0x11, 0x06,  // row 2: the same
	0x0f,
};

typedef union
{
	Uint16 align;  // the offset table is read as Uint16, which needs the storage aligned
	Uint8 bytes[2 + sizeof(qa_cell_body)];
} QaCellFrame;

static Sprite2_array qa_cell_sheet(QaCellFrame *frame)
{
	frame->bytes[0] = 2;  // one-entry offset table, little-endian, pointing just past itself
	frame->bytes[1] = 0;
	memcpy(frame->bytes + 2, qa_cell_body, sizeof(qa_cell_body));
	return (Sprite2_array){ sizeof(frame->bytes), frame->bytes };
}

void qa_enemy_dress_visible(unsigned int slot)
{
	static QaCellFrame frame;
	static Sprite2_array sheet;
	if (sheet.data == NULL)
		sheet = qa_cell_sheet(&frame);

	enemy[slot].sprite2s = &sheet;
	enemy[slot].enemycycle = 1;
	enemy[slot].egr[0] = 1;
}

void qa_enemy_clear_all(void)
{
	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
}

void qa_enemy_stage(unsigned int i, int x, int y, JE_byte armor)
{
	enemyAvail[i] = 0;
	enemy[i].ex = (JE_integer)x;
	enemy[i].ey = (JE_integer)y;
	enemy[i].armorleft = armor;
	enemy[i].enemytype = 1;
	qa_enemy_dress_visible(i);
}

/* Where a projectile is collided from. The geometry comes from sprite data alone, so a synthetic
 * frame pins it exactly; the shot loops in game_loop.c add the target's own middle to the
 * answer. */
void qa_test_shot_hitboxes(void)
{
	QaCellFrame frame;
	const Sprite2_array sheet = qa_cell_sheet(&frame);

	int dx = -1, dy = -1;
	sprite2_center_offset(sheet, 1, &dx, &dy);
	qa_check(dx == 3 && dy == 1, "a frame's middle is the middle of the box its opaque pixels fill");
	sprite2_center_offset(sheet, 2, &dx, &dy);
	qa_check(dx == 0 && dy == 0, "a frame the sheet does not have leaves the point on the blit position");

	const bool savedCentered = centeredShotHitboxes;
	const Sprite2_array savedSheet = spriteSheet8;
	spriteSheet8 = sheet;

	centeredShotHitboxes = false;
	player_shot_hit_offset(1, &dx, &dy);
	qa_check(dx == 0 && dy == 0, "Classic collides a shot from the corner of its sprite cell");
	enemy_shot_hit_offset(1, 0, &dx, &dy);
	qa_check(dx == 0 && dy == 0, "...an enemy shot included");

	centeredShotHitboxes = true;
	player_shot_hit_offset(1, &dx, &dy);
	qa_check(dx == 4 && dy == 1, "Centered collides it from the frame's middle, plus the blit's own pixel");
	enemy_shot_hit_offset(1, 0, &dx, &dy);
	qa_check(dx == 3 && dy == 1, "...and an enemy shot from the frame's middle, which is blitted where it sits");

	const unsigned int optionShapes = sprite_table[OPTION_SHAPES].count;
	sprite_table[OPTION_SHAPES].count = 2;
	player_shot_hit_offset(60001 + 2, &dx, &dy);
	sprite_table[OPTION_SHAPES].count = optionShapes;
	qa_check(optionShapes > 2 && dx == 1 && dy == 0,
	         "a special shot past the sprite table collides from its blit position");

	spriteSheet8 = savedSheet;
	centeredShotHitboxes = savedCentered;
}

// One shot from `bay` at a fixed spot, fired by player 1; MAX_PWEAPON when the gun refused.
static JE_integer qa_guidance_fire(uint bay, JE_word gun)
{
	return player_shot_create(0, bay, 100, 150, 0, 0, gun, 1);
}

static bool qa_guidance_marked(JE_integer id)
{
	return id < MAX_PWEAPON && (playerShotData[id].aimDelayMax & SHOT_AIM_GUIDANCE) != 0;
}

static bool qa_shot_step(JE_integer id)
{
	bool special;
	int x, y, hitDx, hitDy;
	JE_integer damage;
	JE_byte blast, chain, owner;
	JE_word radiusW, radiusH;
	return player_shot_move_and_draw(id, &special, &x, &y, &damage, &blast, &chain, &owner,
	                                 &radiusW, &radiusH, &hitDx, &hitDy);
}

/* Check Guidance Package targeting without changing stock weapon-table homing. */
void qa_test_guidance_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	const bool savedGuidedAim = guidedShotScreenAim;
	const uint savedPower = power;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	guidedShotScreenAim = false;   // the shipped homing is what the stock checks below pin
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);
	power = 10000;   // the guns below must not be refused for generator power
	memset(shotAvail, 0, sizeof(shotAvail));

	const JE_word plainGun = 1;     // Pulse-Cannon level 1: one straight, unguided shot
	const JE_word guidedGun = 587;  // Heavy Guided Bombs: one shot that homes on its own
	qa_check(weapons[plainGun].aim <= 5 && weapons[plainGun].circlesize == 0
	         && weapons[plainGun].multi == 1 && weapons[guidedGun].aim > 5
	         && weapons[guidedGun].multi == 1,
	         "the guidance test's stock guns are the ones it assumes");
	const int guidedOwn = weapons[guidedGun].aim - 5;

	/* Only the shootable hull is valid; the nearer pickup, invulnerable part, and scenery are decoys. */
	qa_enemy_clear_all();
	enemyAvail[0] = 0;
	enemy[0].ex = 120;
	enemy[0].mapoffset = -40;
	enemy[0].ey = 60;
	enemy[0].armorleft = 20;
	enemyAvail[1] = 2;
	enemy[1].ex = 100;
	enemy[1].ey = 100;
	enemy[1].armorleft = 255;
	enemy[1].scoreitem = true;
	enemyAvail[2] = 0;
	enemy[2].ex = 100;
	enemy[2].ey = 120;   // the nearest thing on the field
	enemy[2].armorleft = 255;
	enemyAvail[3] = 2;
	enemy[3].ex = 100;
	enemy[3].ey = 110;
	enemy[3].armorleft = 255;

	// No stacks: nothing changes, and the stock branch aims where it always did.
	JE_integer id = qa_guidance_fire(SHOT_FRONT, plainGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 0 && !qa_guidance_marked(id),
	         "without Guidance Package an unguided gun's shot is not steered");
	id = qa_guidance_fire(SHOT_FRONT, guidedGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 3
	         && playerShotData[id].aimDelayMax == guidedOwn,
	         "...and a stock guided shot keeps its shipped aim: nearest map x, invulnerable part included");
	memset(shotAvail, 0, sizeof(shotAvail));

	// One stack: the main guns steer, the sidekicks and the specials do not.
	endlessPerkTakenBy[0][PERK_GUIDANCE] = 1;
	endlessPerkRederive();
	const JE_integer steered = qa_guidance_fire(SHOT_FRONT, plainGun);
	qa_check(steered < MAX_PWEAPON && playerShotData[steered].aimAtEnemy == 1
	         && playerShotData[steered].aimDelayMax == (ENDLESS_PERK_GUIDANCE_DELAY | SHOT_AIM_GUIDANCE),
	         "one stack steers a front-gun shot at the nearest shootable hull by screen x");
	id = qa_guidance_fire(SHOT_REAR, plainGun);
	qa_check(qa_guidance_marked(id) && playerShotData[id].aimAtEnemy == 1, "...the rear gun's too");
	id = qa_guidance_fire(SHOT_LEFT_SIDEKICK, plainGun);
	qa_check(id < MAX_PWEAPON && !qa_guidance_marked(id) && playerShotData[id].aimAtEnemy == 0,
	         "...but not a sidekick's at one stack");
	id = qa_guidance_fire(SHOT_SPECIAL, plainGun);
	qa_check(id < MAX_PWEAPON && !qa_guidance_marked(id), "...nor a special's at any stack");
	id = qa_guidance_fire(SHOT_FRONT, guidedGun);
	int tightened = guidedOwn - ENDLESS_PERK_GUIDANCE_TIGHTEN;
	if (tightened < 1)
		tightened = 1;
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 1
	         && playerShotData[id].aimDelayMax == (tightened | SHOT_AIM_GUIDANCE),
	         "a gun that homes already turns tighter and is re-aimed at a shootable hull");

	// Two stacks: the sidekicks join and every interval shortens.
	endlessPerkTakenBy[0][PERK_GUIDANCE] = 2;
	endlessPerkRederive();
	id = qa_guidance_fire(SHOT_RIGHT_SIDEKICK, plainGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 1
	         && playerShotData[id].aimDelayMax
	            == ((ENDLESS_PERK_GUIDANCE_DELAY - ENDLESS_PERK_GUIDANCE_STEP) | SHOT_AIM_GUIDANCE),
	         "two stacks steer a sidekick's shot, on the shorter interval");
	id = qa_guidance_fire(SHOT_SPECIAL2, plainGun);
	qa_check(id < MAX_PWEAPON && !qa_guidance_marked(id), "...and still leave the specials alone");

	// Three stacks: the specials join; the superbomb and chained bays never do.
	endlessPerkTakenBy[0][PERK_GUIDANCE] = 3;
	endlessPerkRederive();
	id = qa_guidance_fire(SHOT_SPECIAL, plainGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 1
	         && playerShotData[id].aimDelayMax
	            == ((ENDLESS_PERK_GUIDANCE_DELAY - 2 * ENDLESS_PERK_GUIDANCE_STEP) | SHOT_AIM_GUIDANCE),
	         "three stacks steer a special's shot, on a shorter interval");
	id = qa_guidance_fire(SHOT_SPECIAL2, plainGun);
	qa_check(qa_guidance_marked(id), "...from either special bay");
	id = qa_guidance_fire(SHOT_P1_SUPERBOMB, plainGun);
	qa_check(id < MAX_PWEAPON && !qa_guidance_marked(id), "...but never a superbomb");
	id = qa_guidance_fire(SHOT_MISC, plainGun);
	qa_check(id < MAX_PWEAPON && !qa_guidance_marked(id), "...nor a chained child");

	// Four stacks: no bay gate reaches this far, and the interval bottoms out at every tick.
	qa_check(endlessPerkMaxStack(PERK_GUIDANCE) == 4, "the registry offers a fourth guidance stack");
	endlessPerkTakenBy[0][PERK_GUIDANCE] = 4;
	endlessPerkRederive();
	id = qa_guidance_fire(SHOT_FRONT, plainGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimDelayMax == (1 | SHOT_AIM_GUIDANCE),
	         "four stacks correct on every tick");

	// The correction, from one spot: a steered shot bends toward the screen x, the stock rule
	// toward the map x, and each reloads its own interval.
	PlayerShotDataType *s = &playerShotData[steered];
	s->shotX = 100;
	s->shotXM = 0;
	s->shotYM = -12;
	s->aimDelay = 1;
	player_shot_aim_step(s);
	qa_check(s->shotXM == -1 && s->shotYM == -13 && s->aimDelay == ENDLESS_PERK_GUIDANCE_DELAY,
	         "a steered shot bends toward the enemy's screen x and reloads its interval");
	PlayerShotDataType stock = *s;
	stock.aimDelayMax = (JE_byte)guidedOwn;
	stock.aimAtEnemy = 1;
	stock.shotXM = 0;
	player_shot_aim_step(&stock);
	qa_check(stock.shotXM == 1 && stock.aimDelay == guidedOwn,
	         "...where the stock rule bends toward its map x");

	// The enemy dies with a second hull standing to the right, then the field empties.
	enemyAvail[0] = 1;
	enemyAvail[5] = 0;
	enemy[5].ex = 200;
	enemy[5].mapoffset = 0;
	enemy[5].ey = 60;
	enemy[5].armorleft = 20;
	s->shotXM = 0;
	player_shot_aim_step(s);
	qa_check(s->aimAtEnemy == 6 && s->shotXM == 1,
	         "a steered shot whose enemy died moves on to the next shootable hull");
	stock.shotXM = 0;
	player_shot_aim_step(&stock);
	qa_check(stock.aimAtEnemy == 1 && stock.shotXM == -1,
	         "...while the stock rule veers off, as shipped");
	enemyAvail[5] = 1;
	s->shotXM = 0;
	player_shot_aim_step(s);
	qa_check(s->aimAtEnemy == 0 && s->shotXM == 0,
	         "...and flies straight once nothing shootable is left");

	/* Steering must keep attached and free velocities in their respective ranges. */
	enemyAvail[5] = 0;   // the hull to the upper right stands again
	s->shotX = 100;
	s->shotY = 150;
	s->shotXM = 120;
	s->shotYM = -8;
	player_shot_aim_step(s);
	qa_check(s->aimAtEnemy == 6 && s->shotXM == 121 && s->shotYM == -9,
	         "a shot riding the ship curves toward the enemy inside the ship's frame");
	s->shotXM = SHOT_ATTACHED_VEL_MAX;
	player_shot_aim_step(s);
	qa_check(s->shotXM == SHOT_ATTACHED_VEL_MAX, "...and its riding velocity is capped at the range's top");
	s->shotXM = SHOT_ATTACHED_VEL_MIN - 1;
	player_shot_aim_step(s);
	qa_check(s->shotXM == SHOT_ATTACHED_VEL_MIN - 1,
	         "...while a free velocity never crosses into the riding range");
	enemy[5].ex = 0;   // now to the upper left: the nudges turn negative
	s->shotXM = SHOT_ATTACHED_VEL_MIN + 1;
	s->shotYM = SHOT_ATTACHED_VEL_MIN;
	player_shot_aim_step(s);
	qa_check(s->shotXM == SHOT_ATTACHED_VEL_MIN + 1 && s->shotYM == SHOT_ATTACHED_VEL_MIN,
	         "...a riding velocity never falls out of the range on either axis");
	s->shotXM = SHOT_ATTACHED_VEL_MIN;
	player_shot_aim_step(s);
	qa_check(s->shotXM == SHOT_ATTACHED_VEL_MIN, "...and the value that pins both axes is left alone");
	enemy[5].ex = 200;

	const JE_word ridingGun = 203;  // Zica Laser level 5: every beam rides the ship on x
	qa_check(weapons[ridingGun].sx[0] > 100 && weapons[ridingGun].circlesize == 0,
	         "the guidance test's riding gun is the one it assumes");
	endlessPerkTakenBy[0][PERK_GUIDANCE] = 1;
	endlessPerkRederive();
	id = qa_guidance_fire(SHOT_FRONT, ridingGun);
	qa_check(qa_guidance_marked(id) && playerShotData[id].shotXM >= SHOT_ATTACHED_VEL_MIN
	         && playerShotData[id].aimAtEnemy == 6,
	         "a gun that rides the ship is steered and keeps riding it");

	// The pinned-beam re-encode: both axes become riding velocities the nudge can steer.
	const JE_word pinnedGun = 188;  // Laser level 1: every beam pins to the ship by the x sentinel
	qa_check(weapons[pinnedGun].sx[0] == SHOT_ATTACHED_VEL_MIN && weapons[pinnedGun].sy[0] == 14
	         && weapons[pinnedGun].circlesize == 0 && weapons[pinnedGun].aim <= 5,
	         "the guidance test's pinned gun is the one it assumes");
	id = qa_guidance_fire(SHOT_FRONT, pinnedGun);
	s = &playerShotData[id];
	qa_check(qa_guidance_marked(id) && s->aimAtEnemy == 6
	         && s->shotXM == SHOT_ATTACHED_VEL_REST && s->shotYM == SHOT_ATTACHED_VEL_REST - 14,
	         "a beam pinned by the lone x sentinel is re-encoded to ride the ship on both axes");
	player_shot_aim_step(s);
	qa_check(s->shotXM == SHOT_ATTACHED_VEL_REST + 1 && s->shotYM == SHOT_ATTACHED_VEL_REST - 15,
	         "...and its correction bends both axes inside the ship's frame");

	// The top cull reads ascent from the riding drift.
	const int savedDeltaX = player[0].delta_x_shot_move;
	const int savedDeltaY = player[0].delta_y_shot_move;
	player[0].delta_x_shot_move = 0;
	player[0].delta_y_shot_move = 0;
	s->shotTrail = 255;
	s->shotX = 100;
	s->shotY = -20;
	qa_check(qa_shot_step(id) && shotAvail[id] != 0,
	         "above the screen a re-encoded beam still ascends and survives to the deep margin");
	s->shotY = -20;
	s->shotYM = SHOT_ATTACHED_VEL_REST;
	qa_check(!qa_shot_step(id) && shotAvail[id] == 0,
	         "...while one resting beside the ship is retired at the early one");
	player[0].delta_x_shot_move = savedDeltaX;
	player[0].delta_y_shot_move = savedDeltaY;

	// Patterns: a free-flying one steers, one pinned to the ship on both axes does not.
	const JE_word patternGun = 659;  // The Orange Juicer level 1: one free-flying looping shot
	qa_check(weapons[patternGun].circlesize != 0 && weapons[patternGun].sx[0] == 0
	         && weapons[patternGun].sy[0] == 2 && weapons[patternGun].aim <= 5,
	         "the guidance test's pattern gun is the one it assumes");
	id = qa_guidance_fire(SHOT_FRONT, patternGun);
	s = &playerShotData[id];
	qa_check(qa_guidance_marked(id) && s->shotComplicated && s->aimAtEnemy == 6,
	         "a free-flying pattern shot is steered");
	s->shotXM = 0;
	player_shot_aim_step(s);
	qa_check(s->shotXM == 1, "...by its base velocity, which carries the whole pattern");

	const JE_word ringGun = 749;  // Orange Shield: a pattern pinned to the ship on both axes
	qa_check(weapons[ringGun].circlesize != 0
	         && weapons[ringGun].sx[0] == SHOT_ATTACHED_VEL_REST
	         && weapons[ringGun].sy[0] == SHOT_ATTACHED_VEL_REST && weapons[ringGun].aim <= 5,
	         "the guidance test's ring gun is the one it assumes");
	id = qa_guidance_fire(SHOT_FRONT, ringGun);
	qa_check(id < MAX_PWEAPON && !qa_guidance_marked(id) && playerShotData[id].aimAtEnemy == 0,
	         "a pattern pinned to the ship on both axes is left alone");

	// A recycled slot: the mark must not outlive the shot that carried it.
	memset(shotAvail, 0, sizeof(shotAvail));
	endlessPerkTakenBy[0][PERK_GUIDANCE] = 0;
	endlessPerkRederive();
	id = qa_guidance_fire(SHOT_FRONT, plainGun);
	qa_check(id == steered && !qa_guidance_marked(id) && playerShotData[id].aimAtEnemy == 0,
	         "an unsteered shot in a recycled slot does not inherit the guidance mark");
	memset(shotAvail, 0, sizeof(shotAvail));

	qa_enemy_clear_all();
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	power = savedPower;
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
	guidedShotScreenAim = savedGuidedAim;
}

static const EndlessScoreFind *qa_surveyor_find_at(uint i)
{
	for (unsigned f = 0; f < endlessSurveyorScoreFindCount; ++f)
	{
		const EndlessScoreFind *const find = &endlessSurveyorScoreFinds[f];
		if (enemy[i].enemytype == find->carrier && enemy[i].egr[0] == find->graphic &&
		    enemy[i].evalue == enemy_scaled_value(find->value) &&
		    enemy_explosion_type(&enemy[i]) == find->explosion)
			return find;
	}
	return NULL;
}

// Survey drop fixtures include JE_setupEnemy's scatter RNG draw for each pickup.
static int qa_surveyor_count_loot(int *bombs, int *orbs, int *cubes, int *finds, int *misplaced,
                                  JE_integer wantX, JE_integer wantY)
{
	int loot = 0;
	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		if (enemyAvail[i] != 2)
			continue;
		++loot;
		if (!enemy[i].scoreitem || enemy[i].ex != wantX || enemy[i].ey != wantY)
			++*misplaced;
		if (enemy[i].evalue == -4)
			++*bombs;
		else if (enemy[i].evalue == -3)
			++*orbs;
		else if (enemy[i].evalue == 1)
			++*cubes;
		else if (qa_surveyor_find_at(i) != NULL)
			++*finds;
		else
			++*misplaced;
	}
	return loot;
}

static const EndlessScoreFind *qa_surveyor_find(void)
{
	for (uint i = 0; i < COUNTOF(enemy); ++i)
		if (enemyAvail[i] == 2 && qa_surveyor_find_at(i) != NULL)
			return qa_surveyor_find_at(i);
	return NULL;
}

static JE_word qa_surveyor_collect(int slot, Sint64 *paid)
{
	memset(explosions, 0, sizeof(explosions));
	enemy[slot].ex = player[0].x;
	enemy[slot].ey = player[0].y;
	enemy[slot].mapoffset = 0;
	const Sint64 before = player[0].cash;
	JE_playerCollide(&player[0], 1);
	*paid = player[0].cash - before;
	return enemyAvail[slot] == 1 ? explosions[0].sprite : 0;
}

static int qa_surveyor_draws(void)
{
	int bombs = 0, orbs = 0, cubes = 0, finds = 0, misplaced = 0;
	const int loot = qa_surveyor_count_loot(&bombs, &orbs, &cubes, &finds, &misplaced, 0, 0);
	return 4 + finds + loot;
}

static const EndlessScoreFind *qa_surveyor_catalog(JE_word graphic, JE_integer value)
{
	for (unsigned f = 0; f < endlessSurveyorScoreFindCount; ++f)
	{
		const EndlessScoreFind *const find = &endlessSurveyorScoreFinds[f];
		if (find->graphic == graphic && find->value == value)
			return find;
	}
	return NULL;
}

static bool qa_surveyor_catalog_sound(void)
{
	bool native = true;
	for (JE_word id = 1; id <= ENEMY_NUM; ++id)
	{
		if (enemyDat[id].armor != 0 || enemyDat[id].value < 100 || enemyDat[id].value >= 10000 ||
		    !endlessScoreItemMoves(id))
			continue;
		const bool star = enemyDat[id].egraphic[0] == 57;
		const EndlessScoreFind *const find = qa_surveyor_catalog(enemyDat[id].egraphic[0],
		                                                         star ? 2000 : enemyDat[id].value);
		native = native && find != NULL && (star || find->explosion == enemyDat[id].explosiontype);
	}

	bool falling = true;
	for (unsigned a = 0; a < endlessSurveyorScoreFindCount; ++a)
	{
		const EndlessScoreFind *const fa = &endlessSurveyorScoreFinds[a];
		const int wa = endlessSurveyorScoreWeight(fa);
		falling = falling && wa > 0 && fa->value >= 100 && endlessScoreItemMoves(fa->carrier);
		for (unsigned b = 0; b < endlessSurveyorScoreFindCount; ++b)
		{
			const EndlessScoreFind *const fb = &endlessSurveyorScoreFinds[b];
			const int wb = endlessSurveyorScoreWeight(fb);
			if (fa->value < fb->value)
				falling = falling && wa > wb;
			else if (fa->value == fb->value)
				falling = falling && wa == wb;
		}
	}
	return native && falling;
}

/* Surveyor rolls each linked hull once and places all drops at that hull's kill anchor. */
void qa_test_surveyor_drop_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode, savedExpert = expertMode;
	const JE_shortint savedDifficulty = difficultyLevel;
	const Player savedShip = player[0];
	const Uint64 savedActive = endlessActiveMods;
	Uint64 savedPlayerMods[2];
	memcpy(savedPlayerMods, endlessPlayerMods, sizeof(savedPlayerMods));
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	expertMode = false;
	endlessActiveMods = 0;
	memset(endlessPlayerMods, 0, sizeof(endlessPlayerMods));
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);

	qa_check(enemyDat[800].value == -4 && enemyDat[535].value == -3,
	         "the survey drop test's pickups are the two the collector special-cases");
	qa_check(enemyDat[513].value == 1,
	         "the survey drop test's cube is the datacube the special grant answers");

	difficultyLevel = DIFFICULTY_WIMP;
	const bool wimpPays = enemy_scaled_value(100) == 75;
	difficultyLevel = DIFFICULTY_HARD;
	const bool hardPays = enemy_scaled_value(100) == 112;
	difficultyLevel = DIFFICULTY_NORTANEOUS;
	const bool capped = enemy_scaled_value(5000) == 10000;
	difficultyLevel = DIFFICULTY_NORMAL;
	qa_check(wimpPays && hardPays && capped && enemy_scaled_value(250) == 250 &&
	         enemy_scaled_value(1) == 1 && enemy_scaled_value(-4) == -4 &&
	         enemy_scaled_value(10000) == 10000 && enemy_scaled_value(30001) == 30001,
	         "pickup cash scales with difficulty and leaves special values alone");

	qa_check(qa_surveyor_catalog_sound(),
	         "every pickup worth 100 or more here is a find; cheaper finds are always likelier");

	static const struct
	{
		JE_word    graphic;
		JE_integer value;
		int        share;
	}
	ladder[] = {
		{ 32, 100, 35 }, { 14, 250, 25 }, { 51, 500, 18 }, { 39, 1000, 12 }, { 77, 2500, 7 },
		{ 115, 5000, 3 },
	};
	const EndlessScoreFind *const gem100 = qa_surveyor_catalog(32, 100);
	bool anchored = gem100 != NULL;
	for (unsigned i = 0; i < COUNTOF(ladder) && anchored; ++i)
	{
		const EndlessScoreFind *const gem = qa_surveyor_catalog(ladder[i].graphic, ladder[i].value);
		anchored = gem != NULL && endlessSurveyorScoreWeight(gem) * 35 ==
		                          endlessSurveyorScoreWeight(gem100) * ladder[i].share;
	}
	qa_check(anchored, "the gem ladder keeps its 35/25/18/12/7/3 split from 100 to 5000");

	qa_check(qa_surveyor_catalog(57, 2000) != NULL && qa_surveyor_catalog(57, 100) == NULL &&
	         qa_surveyor_catalog(95, 250) != NULL && qa_surveyor_catalog(245, 800) != NULL &&
	         qa_surveyor_catalog(37, 150) != NULL && qa_surveyor_catalog(241, 375) != NULL,
	         "the purple star only ever drops as the 2K star; candy, orange and ale are finds");
	qa_check(qa_surveyor_catalog(243, 500) == NULL && !endlessScoreItemMoves(475),
	         "the Soh Jin artifact, which never moves, is no find");

	Uint32 total = 0;
	for (unsigned f = 0; f < endlessSurveyorScoreFindCount; ++f)
		total += (Uint32)endlessSurveyorScoreWeight(&endlessSurveyorScoreFinds[f]);
	static int picked[64];
	memset(picked, 0, sizeof(picked));
	bool periodic = endlessSurveyorScoreFindCount <= COUNTOF(picked) &&
	                endlessSurveyorScorePick(0xFFFFFFFFu) != NULL;
	for (Uint32 draw = 0; draw < total && periodic; ++draw)
	{
		const EndlessScoreFind *const pick = endlessSurveyorScorePick(draw);
		periodic = pick != NULL && endlessSurveyorScorePick(draw + total) == pick;
		if (pick != NULL)
			++picked[pick - endlessSurveyorScoreFinds];
	}
	bool weighted = periodic;
	for (unsigned f = 0; f < endlessSurveyorScoreFindCount && weighted; ++f)
		weighted = picked[f] == endlessSurveyorScoreWeight(&endlessSurveyorScoreFinds[f]);
	qa_check(weighted, "the find roll lands on each pickup exactly as often as its weight");

	qa_enemy_clear_all();

	// The loot anchor: a lone cell keeps its own anchor...
	enemyAvail[0] = 0;
	enemy[0].ex = 90;
	enemy[0].ey = 80;
	JE_integer ax, ay;
	enemy_loot_anchor(0, &ax, &ay);
	qa_check(ax == 90 && ay == 80, "a lone cell's loot anchor is its own");

	// ...and a linked 2x2-of-cells hull centers a loot cell on its visible union.
	for (uint part = 10; part < 14; ++part)
	{
		enemyAvail[part] = 0;
		enemy[part].ex = (JE_integer)(100 + (part % 2) * 12);
		enemy[part].ey = (JE_integer)(60 + ((part - 10) / 2) * 14);
		enemy[part].linknum = 9;
	}
	enemy_loot_anchor(10, &ax, &ay);
	qa_check(ax == 106 && ay == 67, "a linked hull's loot anchor is the middle of its union");

	// Without the perk a kill leaves the shared stream untouched and the field empty.
	for (uint i = 0; i < COUNTOF(enemy); ++i)
		enemyAvail[i] = 1;
	mt_srand(0x5EED50);
	const Uint32 refDraw = mt_rand();
	mt_srand(0x5EED50);
	enemyAvail[0] = 0;
	enemy[0].linknum = 0;
	enemy_logical_death(0, 0);
	qa_check(mt_rand() == refDraw, "without Surveyor a kill draws nothing from the shared stream");
	bool spawnedAny = false;
	for (uint i = 0; i < COUNTOF(enemy); ++i)
		spawnedAny |= enemyAvail[i] != 1;
	qa_check(!spawnedAny, "...and drops nothing");

	/* Two stacks: run kills in the first free slot of the bank; both finds must show up. A drop
	 * landing in that slot instead of beside it would mean the occupied-slot hold is gone. */
	endlessPerkTakenBy[0][PERK_SURVEYOR] = 2;
	endlessPerkRederive();
	mt_srand(0x5EED51);
	int bombs = 0, orbs = 0, cubes = 0, finds = 0, misplaced = 0;
	for (int k = 0; k < 3000; ++k)
	{
		for (uint i = 0; i < COUNTOF(enemy); ++i)
			enemyAvail[i] = 1;   // sweep the last pass's loot so the bank cannot fill up
		enemyAvail[0] = 0;
		enemy[0].ex = (JE_integer)(60 + (k % 100));
		enemy[0].ey = 90;
		enemy[0].linknum = 0;
		enemy_logical_death(0, 0);
		qa_surveyor_count_loot(&bombs, &orbs, &cubes, &finds, &misplaced, enemy[0].ex, enemy[0].ey);
	}
	qa_check(bombs > 0 && orbs > 0 && cubes > 0 && finds > 0,
	         "with Surveyor all four survey pickups drop");
	qa_check(cubes < bombs + orbs, "the cube drops at longer odds than the survey pair");
	qa_check(misplaced == 0,
	         "every drop is a score item at the kill anchor, outside the freed kill slot");

	// One stack: half the drop chance of two, pinned by comparing the same-size runs.
	endlessPerkTakenBy[0][PERK_SURVEYOR] = 1;
	endlessPerkRederive();
	mt_srand(0x5EED54);
	int bombs1 = 0, orbs1 = 0, cubes1 = 0, finds1 = 0, misplaced1 = 0;
	for (int k = 0; k < 3000; ++k)
	{
		for (uint i = 0; i < COUNTOF(enemy); ++i)
			enemyAvail[i] = 1;
		enemyAvail[0] = 0;
		enemy[0].linknum = 0;
		enemy_logical_death(0, 0);
		qa_surveyor_count_loot(&bombs1, &orbs1, &cubes1, &finds1, &misplaced1, enemy[0].ex,
		                       enemy[0].ey);
	}
	qa_check(bombs1 + orbs1 + cubes1 + finds1 > 0 &&
	         bombs1 + orbs1 + cubes1 + finds1 < bombs + orbs + cubes + finds && misplaced1 == 0,
	         "one stack still drops, at longer odds than two");

	bool oneIn = true;
	int reskinned = 0;
	for (JE_byte stacks = 1; stacks <= 2; ++stacks)
	{
		endlessPerkTakenBy[0][PERK_SURVEYOR] = stacks;
		endlessPerkRederive();
		const Uint32 odds = stacks == 1 ? 66 : 33;
		for (Uint32 k = 0; k < 4000; ++k)
		{
			for (uint i = 0; i < COUNTOF(enemy); ++i)
				enemyAvail[i] = 1;
			enemyAvail[0] = 0;
			enemy[0].linknum = 0;
			const Uint32 seed = 0x5C0E0000u + stacks * 0x10000u + k;
			mt_srand(seed);
			enemy_logical_death(0, 0);
			const EndlessScoreFind *const got = qa_surveyor_find();

			mt_srand(seed);
			for (int d = 0; d < 3; ++d)
				mt_rand();
			const bool scored = mt_rand() % odds == 0;
			const EndlessScoreFind *const want = scored ? endlessSurveyorScorePick(mt_rand())
			                                            : NULL;
			oneIn = oneIn && got == want;
			reskinned += got != NULL && enemyDat[got->carrier].egraphic[0] != got->graphic;
		}
	}
	qa_check(oneIn, "a kill's score roll lands 1 in 66 with one stack and 1 in 33 with two, "
	                "and drops exactly the pickup its pick draw names");
	qa_check(reskinned > 0,
	         "...including other episodes' pickups, in their own sprite, value and popup");

	const EndlessScoreFind *const star = qa_surveyor_catalog(57, 2000);
	endlessPerkTakenBy[0][PERK_SURVEYOR] = 2;
	endlessPerkRederive();
	int starSlot = -1;
	for (Uint32 k = 0; k < 40000 && star != NULL && starSlot < 0; ++k)
	{
		for (uint i = 0; i < COUNTOF(enemy); ++i)
			enemyAvail[i] = 1;
		enemyAvail[0] = 0;
		enemy[0].linknum = 0;
		mt_srand(0x57A20000u + k);
		enemy_logical_death(0, 0);
		for (uint i = 0; i < COUNTOF(enemy) && starSlot < 0; ++i)
			if (enemyAvail[i] == 2 && qa_surveyor_find_at(i) == star)
				starSlot = (int)i;
	}
	JE_word starPop = 0, gemPop = 0, carrierPop = 0;
	Sint64 starPaid = 0, gemPaid = 0, carrierPaid = 0;
	if (starSlot >= 0)
	{
		for (uint i = 0; i < COUNTOF(enemy); ++i)
			if ((int)i != starSlot)
				enemyAvail[i] = 1;
		player[0].is_alive = true;
		player[0].x = 100;
		player[0].y = 150;
		starPop = qa_surveyor_collect(starSlot, &starPaid);
		const Sint16 gem = JE_newEnemy(0, 398, 0);
		if (gem > 0)
			gemPop = qa_surveyor_collect(gem - 1, &gemPaid);
		const Sint16 carrier = JE_newEnemy(0, star->carrier, 0);
		if (carrier > 0)
			carrierPop = qa_surveyor_collect(carrier - 1, &carrierPaid);
	}
	qa_check(starSlot >= 0 && starPaid == 2000 && starPop != 0 && starPop == gemPop &&
	         starPop != carrierPop && carrierPaid != starPaid,
	         "a carried pickup pays and pops as itself: the 2K star, not the pickup carrying it");

	bool shunned = star != NULL;
	for (int variant = 0; variant < 2 && star != NULL; ++variant)
	{
		const JE_byte savedCarrierArmor = enemyDat[star->carrier].armor;
		const JE_shortint savedCarrierFall = enemyDat[star->carrier].ymove;
		if (variant == 0)
			enemyDat[star->carrier].armor = 10;
		else
			enemyDat[star->carrier].ymove = 0;
		for (unsigned f = 0; f < endlessSurveyorScoreFindCount; ++f)
			if (endlessSurveyorScoreFinds[f].carrier == star->carrier)
				shunned = shunned && endlessSurveyorScoreWeight(&endlessSurveyorScoreFinds[f]) == 0;
		for (Uint32 draw = 0; draw < 4000 && shunned; ++draw)
		{
			const EndlessScoreFind *const pick = endlessSurveyorScorePick(draw);
			shunned = pick != NULL && pick->carrier != star->carrier;
		}
		enemyDat[star->carrier].armor = savedCarrierArmor;
		enemyDat[star->carrier].ymove = savedCarrierFall;
	}
	qa_check(shunned,
	         "a find whose carrier is no pickup in the loaded episode, or sits still, never drops");

	/* A linked hull spends one triple of draws however many parts go down with it, plus the one
	 * scatter draw each spawned pickup costs, and its loot sits at the hull's middle. */
	endlessPerkTakenBy[0][PERK_SURVEYOR] = 2;
	endlessPerkRederive();
	int hullDrops = 0, hullMisplaced = 0, hullBombs = 0, hullOrbs = 0, hullCubes = 0, hullFinds = 0;
	mt_srand(0x5EED52);
	for (int k = 0; k < 2000 && hullDrops == 0; ++k)
	{
		for (uint i = 0; i < COUNTOF(enemy); ++i)
			enemyAvail[i] = 1;
		const JE_byte link = (JE_byte)(9 + (k % 2));   // alternate so the link latch never blocks
		for (uint part = 10; part < 14; ++part)
		{
			enemyAvail[part] = 0;
			enemy[part].ex = (JE_integer)(100 + (part % 2) * 12);
			enemy[part].ey = (JE_integer)(60 + ((part - 10) / 2) * 14);
			enemy[part].linknum = link;
		}
		const Uint32 markBefore = mt_rand();
		mt_srand(markBefore);   // reseed on a drawn value: a fresh, reproducible stream point
		for (uint part = 10; part < 14; ++part)
			enemy_logical_death(part, 0);
		hullDrops += qa_surveyor_count_loot(&hullBombs, &hullOrbs, &hullCubes, &hullFinds,
		                                    &hullMisplaced, 106, 67);
		const Uint32 got = mt_rand();
		mt_srand(markBefore);
		for (int d = 0; d < 4 + hullFinds + hullDrops; ++d)
			mt_rand();
		qa_check(mt_rand() == got,
		         "a linked hull spends one set of survey draws plus its spawns' scatter, "
		         "not one per part");
		if (hullDrops == 0)
			mt_srand(got);   // walk the stream forward for the next attempt
	}
	qa_check(hullDrops > 0 && hullMisplaced == 0,
	         "a linked hull's loot lands at the middle of its union");

	// In co-op the perk is the holder's: only their kills roll, though anyone may take the loot.
	coopEndlessMode = true;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkTakenBy[1][PERK_SURVEYOR] = 2;
	endlessPerkRederive();
	for (uint i = 0; i < COUNTOF(enemy); ++i)
		enemyAvail[i] = 1;
	mt_srand(0x5EED53);
	const Uint32 coopRef = mt_rand();
	mt_srand(0x5EED53);
	enemyAvail[5] = 0;
	enemy[5].linknum = 0;
	enemy_logical_death(5, 0);   // killed by the seat without the perk
	qa_check(mt_rand() == coopRef, "in co-op the bare seat's kills roll nothing");

	for (uint i = 0; i < COUNTOF(enemy); ++i)
		enemyAvail[i] = 1;
	mt_srand(0x5EED55);
	enemyAvail[5] = 0;
	enemy[5].linknum = 0;
	enemy_logical_death(5, 1);   // killed by the holder
	const int coopDraws = qa_surveyor_draws();
	const Uint32 gotCoop = mt_rand();
	mt_srand(0x5EED55);
	for (int d = 0; d < coopDraws; ++d)
		mt_rand();
	qa_check(mt_rand() == gotCoop, "...the holder's kills roll");

	for (uint i = 0; i < COUNTOF(enemy); ++i)
		enemyAvail[i] = 1;
	mt_srand(0x5EED56);
	enemyAvail[5] = 0;
	enemy[5].linknum = 0;
	enemy_logical_death(5, ENDLESS_KILLER_NONE);
	const int noneDraws = qa_surveyor_draws();
	const Uint32 gotNone = mt_rand();
	mt_srand(0x5EED56);
	for (int d = 0; d < noneDraws; ++d)
		mt_rand();
	qa_check(mt_rand() == gotNone, "...and an unclaimed kill rolls with the deeper seat");

	const JE_boolean savedNet = isNetworkGame, savedTwo = twoPlayerMode;
	const bool savedHost = network_is_host;
	const uint savedThis = thisPlayerNum;
	enum { TALLY = 3 + 64 };
	static int tally[4][TALLY];
	Uint32 streamEnd[4];
	memset(tally, 0, sizeof(tally));
	for (uint m = 0; m < 4; ++m)
	{
		isNetworkGame = m != 0;
		thisPlayerNum = m == 2 ? 2 : 1;
		network_is_host = m == 1;
		twoPlayerMode = true;
		coopEndlessMode = true;
		memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
		endlessPerkTakenBy[0][PERK_SURVEYOR] = 1;
		endlessPerkTakenBy[1][PERK_SURVEYOR] = 2;
		endlessPerkRederive();
		endlessSetFxPlayer(m % 2);
		mt_srand(0x5A11A1u);
		for (int k = 0; k < 6000; ++k)
		{
			for (uint i = 0; i < COUNTOF(enemy); ++i)
				enemyAvail[i] = 1;
			enemyAvail[0] = 0;
			enemy[0].linknum = 0;
			enemy_logical_death(0, k % 3 == 2 ? ENDLESS_KILLER_NONE : k % 3);
			qa_surveyor_count_loot(&tally[m][0], &tally[m][1], &tally[m][2], &tally[m][3],
			                       &tally[m][4], enemy[0].ex, enemy[0].ey);
			const EndlessScoreFind *const find = qa_surveyor_find();
			if (find != NULL && endlessSurveyorScoreFindCount <= TALLY - 5)
				++tally[m][5 + (find - endlessSurveyorScoreFinds)];
		}
		streamEnd[m] = mt_rand();
	}
	bool sameEverywhere = tally[0][3] > 0;
	for (uint m = 1; m < 4; ++m)
		sameEverywhere = sameEverywhere && streamEnd[m] == streamEnd[0] &&
		                 memcmp(tally[m], tally[0], sizeof(tally[0])) == 0;
	qa_check(sameEverywhere,
	         "both machines, online or local, drop the same finds from either seat's kills");
	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	network_is_host = savedHost;
	thisPlayerNum = savedThis;
	endlessSetFxPlayer(0);

	qa_enemy_clear_all();
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	memcpy(endlessPlayerMods, savedPlayerMods, sizeof(savedPlayerMods));
	endlessActiveMods = savedActive;
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
	expertMode = savedExpert;
	difficultyLevel = savedDifficulty;
	player[0] = savedShip;
	memset(explosions, 0, sizeof(explosions));
}

/* Twin Pods: where the two volleys land around each pod, that the perk stays personal in co-op, and
 * the two refusals (no first volley, no generator power for a second). */
void qa_test_twin_pods_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	const uint savedPower = power;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);
	power = 10000;
	memset(shotAvail, 0, sizeof(shotAvail));

	const JE_word podPort = 16;   // Miscellaneous Option Weapons: the port every stock sidekick fires through
	const JE_word podGun = 1;     // Pulse-Cannon level 1: one shot, one pattern position
	qa_check(weaponPort[podPort].poweruse > 0 && weapons[podGun].multi == 1 && weapons[podGun].max <= 1,
	         "the twin pods test's port and gun are the ones it assumes");
	const int off = ENDLESS_PERK_TWINPODS_SPREAD_PX / 2;

	// Without the perk there is no offset and nothing fires. The lone shot marks the pod's centre line.
	qa_check(endlessPerkTwinPodOffset(0, LEFT_SIDEKICK) == 0 && endlessPerkTwinPodOffset(0, RIGHT_SIDEKICK) == 0,
	         "without Twin Pods a sidekick has no twin offset");
	JE_integer first = player_shot_create(podPort, SHOT_LEFT_SIDEKICK, 100, 150, 0, 0, podGun, 1);
	qa_check(first < MAX_PWEAPON
	         && player_shot_create_twin(first, podPort, LEFT_SIDEKICK, 0, 100, 150, 0, 0, podGun, 1) == MAX_PWEAPON,
	         "...and no twin volley fires");
	const int centreX = (first < MAX_PWEAPON) ? playerShotData[first].shotX : 0;
	memset(shotAvail, 0, sizeof(shotAvail));

	// The perk: the twin half the spread outboard of each pod, so the pair mirrors across the ship.
	endlessPerkTakenBy[0][PERK_TWINPODS] = 1;
	endlessPerkRederive();
	qa_check(endlessPerkTwinPodOffset(0, LEFT_SIDEKICK) == -off && endlessPerkTwinPodOffset(0, RIGHT_SIDEKICK) == off,
	         "the twin leaves half the spread left of the left pod and right of the right pod");
	endlessMode = false;
	qa_check(endlessPerkTwinPodOffset(0, LEFT_SIDEKICK) == 0, "...but only in an endless run");
	endlessMode = true;
	qa_check(endlessPerkTwinPodOffset(1, LEFT_SIDEKICK) == -off,
	         "...and outside co-op a second ship flies off the only perk row there is");

	/* Auxiliary Overdrive uses the firing ship's stacks, not the current effect context. */
	coopEndlessMode = true;
	endlessSetFxPlayer(0);
	qa_check(endlessPerkTwinPodOffset(1, LEFT_SIDEKICK) == 0 && endlessPerkTwinPodOffset(0, LEFT_SIDEKICK) == -off,
	         "a partner without Twin Pods keeps single pods while the holder's twin");
	endlessSetFxPlayer(1);
	qa_check(endlessPerkTwinPodOffset(1, LEFT_SIDEKICK) == 0 && endlessPerkTwinPodOffset(0, LEFT_SIDEKICK) == -off,
	         "...whichever ship the effect context names");
	endlessPerkTakenBy[1][PERK_TWINPODS] = 1;
	endlessPerkRederive();
	qa_check(endlessPerkTwinPodOffset(1, RIGHT_SIDEKICK) == off,
	         "...and a partner who takes it too gets the same spread");
	endlessPerkTakenBy[1][PERK_TWINPODS] = 0;
	endlessPerkRederive();
	coopEndlessMode = false;
	endlessSetFxPlayer(0);

	// As the fire sites do it: the own volley the same distance inboard, the twin from the pod's x.
	const int leftDx = endlessPerkTwinPodOffset(0, LEFT_SIDEKICK),
	          rightDx = endlessPerkTwinPodOffset(0, RIGHT_SIDEKICK);
	first = player_shot_create(podPort, SHOT_LEFT_SIDEKICK, 100 - leftDx, 150, 0, 0, podGun, 1);
	JE_integer twin = player_shot_create_twin(first, podPort, LEFT_SIDEKICK, leftDx,
	                                          100, 150, 0, 0, podGun, 1);
	qa_check(first < MAX_PWEAPON && twin < MAX_PWEAPON && twin != first
	         && playerShotData[first].shotX == centreX + off && playerShotData[twin].shotX == centreX - off
	         && playerShotData[twin].shotY == playerShotData[first].shotY
	         && playerShotData[twin].shotDmg == playerShotData[first].shotDmg,
	         "a left pod's two volleys are the same shot, centred on the pod, the twin outboard");
	first = player_shot_create(podPort, SHOT_RIGHT_SIDEKICK, 100 - rightDx, 150, 0, 0, podGun, 1);
	twin = player_shot_create_twin(first, podPort, RIGHT_SIDEKICK, rightDx, 100, 150, 0, 0, podGun, 1);
	qa_check(twin < MAX_PWEAPON && playerShotData[first].shotX == centreX - off
	         && playerShotData[twin].shotX == centreX + off,
	         "...and a right pod's mirror them");
	qa_check(player_shot_create_twin(MAX_PWEAPON, podPort, LEFT_SIDEKICK, -off,
	                                 100, 150, 0, 0, podGun, 1) == MAX_PWEAPON,
	         "a refused first volley fires no twin");
	memset(shotAvail, 0, sizeof(shotAvail));

	// The twin pays the generator itself: enough for one volley is enough for one.
	const uint volleyCost = weaponPort[podPort].poweruse;
	power = volleyCost + volleyCost / 2;
	first = player_shot_create(podPort, SHOT_LEFT_SIDEKICK, 100, 150, 0, 0, podGun, 1);
	twin = player_shot_create_twin(first, podPort, LEFT_SIDEKICK, -off, 100, 150, 0, 0, podGun, 1);
	qa_check(first < MAX_PWEAPON && twin == MAX_PWEAPON && power == volleyCost / 2,
	         "a generator that can pay for one volley fires the pod's own and refuses the twin");
	power = 2 * volleyCost;
	first = player_shot_create(podPort, SHOT_LEFT_SIDEKICK, 100, 150, 0, 0, podGun, 1);
	twin = player_shot_create_twin(first, podPort, LEFT_SIDEKICK, -off, 100, 150, 0, 0, podGun, 1);
	qa_check(first < MAX_PWEAPON && twin < MAX_PWEAPON && power == 0,
	         "...and one that can pay for two fires both, spending both");
	memset(shotAvail, 0, sizeof(shotAvail));

	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	power = savedPower;
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
}

/* Reinforced Prow: the ram figures per stack, the invulnerable-ram cadence, and the Endless ram
 * kill going through the shot's own destruction walk (payout, tally, a linked hull down whole). */
void qa_test_reinforced_prow_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	struct JE_SingleEnemyType savedEnemy[8];
	JE_byte savedAvail[COUNTOF(savedEnemy)];
	memcpy(savedEnemy, enemy, sizeof(savedEnemy));
	memcpy(savedAvail, enemyAvail, sizeof(savedAvail));
	const JE_word savedKilled = enemyKilled;
	const Sint64 savedCash = player[0].cash;
	const Uint64 savedEarned = endlessRunCashEarned, savedSpent = endlessRunCashSpent;
	Uint64 savedSources[ENDLESS_CASH_SOURCES], savedSinks[ENDLESS_CASH_SINKS];
	memcpy(savedSources, endlessCashBySource, sizeof(savedSources));
	memcpy(savedSinks, endlessCashBySink, sizeof(savedSinks));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);
	endlessCashResync();

	qa_check(endlessPerkProwRamDamage(2) == 2 && endlessPerkProwContactPercent() == 100,
	         "without Reinforced Prow a ram deals and costs the stock figure");
	static const int dealt[] = { 4, 6, 8, 10 };
	static const int taken[] = { 78, 56, 34, 12 };
	bool ladder = true;
	for (int s = 1; s <= 4; ++s)
	{
		endlessPerkTakenBy[0][PERK_PROW] = (JE_byte)s;
		endlessPerkRederive();
		ladder &= endlessPerkProwRamDamage(2) == dealt[s - 1]
		       && endlessPerkProwContactPercent() == taken[s - 1];
	}
	qa_check(ladder, "the stacks deal x2 to x5 and take 78%, 56%, 34%, 12%");
	endlessMode = false;
	qa_check(endlessPerkProwRamDamage(2) == 2 && endlessPerkProwContactPercent() == 100,
	         "...only in an endless run");
	endlessMode = true;

	/* Ram damage adds Salvo and Knife Fight bonuses without increasing self-damage. */
	const int savedWindow = endlessSalvoWindow[0];
	endlessSalvoWindow[0] = 0;
	const int stockRam = endlessPerkProwRamDamage(2);
	const int knifeRam = endlessPerkKnifeFightBonus(stockRam, 50);
	qa_check(stockRam == 10 && knifeRam == 5
	         && endlessOpeningSalvoScale(stockRam) + knifeRam == 15,
	         "with no salvo window a four-stack ram is its stock x5 plus Knife Fight");
	endlessSalvoWindow[0] = ENDLESS_PERK_SALVO_WINDOW;
	qa_check(endlessOpeningSalvoScale(stockRam) == 25
	         && endlessOpeningSalvoScale(stockRam) + knifeRam == 30,
	         "...and an open Opening Salvo window lifts that ram 2.5x, Knife Fight added to it");
	qa_check(endlessPerkKnifeFightBonus(stockRam, 50) == knifeRam,
	         "...and Knife Fight's bonus reads the same inside the window as outside");
	qa_check(endlessPerkProwContactPercent() == 12,
	         "...and never touching what that ram costs the ship");
	endlessSalvoWindow[0] = savedWindow;

	// Personal: the partner rams on its own stacks.
	coopEndlessMode = true;
	endlessSetFxPlayer(1);
	qa_check(endlessPerkProwRamDamage(2) == 2 && endlessPerkProwContactPercent() == 100,
	         "a co-op partner without the perk rams at stock");
	endlessSetFxPlayer(0);
	qa_check(endlessPerkProwRamDamage(2) == 10, "...while the holder rams at its stacks");
	coopEndlessMode = false;

	// An invulnerable ship lands a ram every tenth tick of its window in Endless, and outside it
	// only with the Ramming rework.
	qa_check(endlessRamWhileInvulnerable(20) && endlessRamWhileInvulnerable(10)
	         && !endlessRamWhileInvulnerable(19) && !endlessRamWhileInvulnerable(1),
	         "an invulnerable ship rams on the tick cadence");
	const bool savedRamming = reworkEnabled[REWORK_RAMMING];
	endlessMode = false;
	reworkEnabled[REWORK_RAMMING] = false;
	qa_check(!endlessRamWhileInvulnerable(20),
	         "...and outside Endless never without the Ramming rework");
	reworkEnabled[REWORK_RAMMING] = true;
	qa_check(endlessRamWhileInvulnerable(20) && !endlessRamWhileInvulnerable(19),
	         "...which brings the same cadence to the other modes");
	reworkEnabled[REWORK_RAMMING] = savedRamming;
	endlessMode = true;

	/* The Endless ram kill: enemy_kill_group takes a lone enemy and pays it out, and takes a linked
	 * hull whole. Both are what a killing shot does. */
	for (uint i = 0; i < COUNTOF(savedEnemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
	enemyAvail[0] = 0;
	enemy[0].ex = 100;
	enemy[0].ey = 100;
	enemy[0].armorleft = 1;
	enemy[0].enemytype = 1;
	enemy[0].evalue = 300;
	const Sint64 loneCash = player[0].cash;
	const JE_word loneKilled = enemyKilled;
	enemy_kill_group(0, 0, 0);
	qa_check(enemyAvail[0] == 1 && player[0].cash == loneCash + 300 && enemyKilled == loneKilled + 1,
	         "a ram kill removes the enemy, pays its worth and counts");

	for (uint i = 0; i < 3; ++i)
	{
		enemyAvail[i] = 0;
		enemy[i].ex = (JE_integer)(100 + i * 8);
		enemy[i].ey = 100;
		enemy[i].linknum = 7;
		enemy[i].armorleft = 1;
		enemy[i].enemytype = 1;
		enemy[i].evalue = 100;
	}
	enemyAvail[3] = 0;   // a bystander with another link stays
	enemy[3].ex = 200;
	enemy[3].ey = 100;
	enemy[3].linknum = 9;
	enemy[3].armorleft = 5;
	enemy[3].enemytype = 1;
	const Sint64 hullCash = player[0].cash;
	enemy_kill_group(1, 0, 0);
	qa_check(enemyAvail[0] == 1 && enemyAvail[1] == 1 && enemyAvail[2] == 1 && enemyAvail[3] == 0
	         && player[0].cash == hullCash + 300,
	         "...and a linked hull goes down whole, every tile paid, its neighbour untouched");
	JE_resetSP();
	memset(explosions, 0, sizeof(explosions));   // the deaths above puffed; explosions[] is registered

	enemyKilled = savedKilled;
	memcpy(enemy, savedEnemy, sizeof(savedEnemy));
	memcpy(enemyAvail, savedAvail, sizeof(savedAvail));
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	player[0].cash = savedCash;
	endlessRunCashEarned = savedEarned;
	endlessRunCashSpent = savedSpent;
	memcpy(endlessCashBySource, savedSources, sizeof(savedSources));
	memcpy(endlessCashBySink, savedSinks, sizeof(savedSinks));
	endlessCashResync();
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
}

/* Knife Fight: the hull-to-hull gap, the bonus ladder over it, a linked hull measured to its nearest
 * tile, and the perk staying with the ship that flies close in co-op. */
void qa_test_knife_fight_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	struct JE_SingleEnemyType savedEnemy[8];
	JE_byte savedAvail[COUNTOF(savedEnemy)];
	memcpy(savedEnemy, enemy, sizeof(savedEnemy));
	memcpy(savedAvail, enemyAvail, sizeof(savedAvail));
	const Player savedPlayers[2] = { player[0], player[1] };

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);
	for (uint i = 0; i < COUNTOF(savedEnemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}

	// The ship's hull covers x-5..x+18, y-7..y+20; a lone 12x14 tile at (ex, ey) covers ex..ex+11,
	// ey..ey+13. With the ship at (100, 150) a tile at (100, ey) sits 129 - ey px above it.
	player[0].x = 100;
	player[0].y = 150;
	enemyAvail[0] = 0;
	enemy[0].ex = 100;
	enemy[0].ey = 100;
	enemy[0].armorleft = 50;
	enemy[0].enemytype = 1;
	qa_check(endlessShipHullGapPx(0, 0) == 29, "the gap is measured between the two hulls' edges");
	enemy[0].ey = 130;
	qa_check(endlessShipHullGapPx(0, 0) == 0, "...and is zero once they overlap");
	enemy[0].ey = 100;
	enemy[0].ex = 160;   // 41 px clear on x, 29 on y: the wider clearance is the gap
	qa_check(endlessShipHullGapPx(0, 0) == 41, "...and the larger per-axis clearance decides");
	enemy[0].ex = 100;
	enemy[0].size = 1;   // a 2x2 body reaches 7 px further down
	qa_check(endlessShipHullGapPx(0, 0) == 22, "a 2x2 body is measured to its outer edge");
	enemy[0].size = 0;

	qa_check(endlessPerkKnifeFightPercent(0) == 0
	         && endlessPerkKnifeFightBonus(20, endlessPerkKnifeFightPercent(0)) == 0,
	         "without Knife Fight there is no bonus");
	endlessPerkTakenBy[0][PERK_KNIFE] = 4;
	endlessPerkRederive();
	const int fullY = 129 - ENDLESS_PERK_KNIFE_FULL_PX;   // the tile that sits exactly the full gap away
	enemy[0].ey = (JE_integer)fullY;
	qa_check(endlessPerkKnifeFightPercent(0) == 4 * ENDLESS_PERK_KNIFE_PCT
	         && endlessPerkKnifeFightBonus(20, endlessPerkKnifeFightPercent(0))
	            == 20 * 4 * ENDLESS_PERK_KNIFE_PCT / 100,
	         "at the full gap four stacks give the whole bonus");
	enemy[0].ey = 130;
	qa_check(endlessPerkKnifeFightPercent(0) == 4 * ENDLESS_PERK_KNIFE_PCT,
	         "...touching gives the same");
	enemy[0].ey = (JE_integer)(fullY - ENDLESS_PERK_KNIFE_FADE_PX / 2);   // half way through the fade
	qa_check(endlessPerkKnifeFightPercent(0) == 4 * ENDLESS_PERK_KNIFE_PCT / 2,
	         "half way out it is half");
	enemy[0].ey = (JE_integer)(fullY - ENDLESS_PERK_KNIFE_FADE_PX);
	qa_check(endlessPerkKnifeFightPercent(0) == 0, "...and at the end of the fade nothing");
	enemy[0].ey = (JE_integer)fullY;
	endlessPerkTakenBy[0][PERK_KNIFE] = 1;
	endlessPerkRederive();
	qa_check(endlessPerkKnifeFightPercent(0) == ENDLESS_PERK_KNIFE_PCT, "one stack is one step");
	endlessMode = false;
	qa_check(endlessPerkKnifeFightPercent(0) == 0, "...only in an endless run");
	endlessMode = true;

	// A linked hull is measured to whichever tile is nearest, though the hit landed elsewhere.
	enemy[0].ey = 100;
	enemy[0].linknum = 7;
	enemyAvail[1] = 0;
	enemy[1].ex = 100;
	enemy[1].ey = (JE_integer)fullY;
	enemy[1].linknum = 7;
	enemy[1].armorleft = 50;
	enemy[1].enemytype = 1;
	qa_check(endlessShipHullGapPx(0, 0) == ENDLESS_PERK_KNIFE_FULL_PX
	         && endlessPerkKnifeFightPercent(0) == ENDLESS_PERK_KNIFE_PCT,
	         "a hit on a far tile of a linked hull is measured to its nearest tile");
	enemyAvail[1] = 1;
	qa_check(endlessShipHullGapPx(0, 0) == 29, "...a dead tile no longer counts");
	enemy[0].linknum = 0;

	// Personal, and measured from the owner's own ship: the partner is far away, the holder close.
	coopEndlessMode = true;
	player[1].x = 100;
	player[1].y = 150;
	player[0].x = 20;
	player[0].y = 20;
	enemy[0].ey = (JE_integer)fullY;
	endlessPerkTakenBy[0][PERK_KNIFE] = 0;
	endlessPerkTakenBy[1][PERK_KNIFE] = 2;
	endlessPerkRederive();
	endlessSetFxPlayer(1);
	qa_check(endlessPerkKnifeFightPercent(0) == 2 * ENDLESS_PERK_KNIFE_PCT,
	         "in co-op the holder's shot is measured from the holder's ship");
	endlessSetFxPlayer(0);
	qa_check(endlessPerkKnifeFightPercent(0) == 0, "...and the partner without it gets nothing");
	endlessSetFxPlayer(1);
	player[1].y = 20;
	qa_check(endlessPerkKnifeFightPercent(0) == 0, "...nor the holder from far away");
	coopEndlessMode = false;
	endlessSetFxPlayer(0);

	player[0] = savedPlayers[0];
	player[1] = savedPlayers[1];
	memcpy(enemy, savedEnemy, sizeof(savedEnemy));
	memcpy(enemyAvail, savedAvail, sizeof(savedAvail));
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
}

// Deflector damage, ownership, shot reversal, and Opening Salvo inheritance.
void qa_test_deflector_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	JE_byte savedPerks[2][PERK_COUNT];
	int savedSalvo[2];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	memcpy(savedSalvo, endlessSalvoWindow, sizeof(savedSalvo));
	memset(endlessSalvoWindow, 0, sizeof(endlessSalvoWindow));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);

	qa_check(endlessPerkDeflectDamage(6) == 0, "without Deflector a shield hit returns nothing");
	endlessPerkTakenBy[0][PERK_DEFLECTOR] = 1;
	endlessPerkRederive();
	qa_check(endlessPerkDeflectDamage(6) == 6 && endlessPerkDeflectDamage(0) == 0,
	         "one stack returns the absorbed damage, and nothing for nothing");
	endlessPerkTakenBy[0][PERK_DEFLECTOR] = 2;
	endlessPerkRederive();
	qa_check(endlessPerkDeflectDamage(6) == 12, "two stacks return it doubled");
	qa_check(endlessPerkDeflectDamage(200) == 249 && endlessPerkDeflectDamage(50) == 100,
	         "...clear of the piercing and ice markers");
	endlessMode = false;
	qa_check(endlessPerkDeflectDamage(6) == 0, "...only in an endless run");
	endlessMode = true;

	coopEndlessMode = true;
	endlessSetFxPlayer(1);
	qa_check(endlessPerkDeflectDamage(6) == 0, "a co-op partner without the perk returns nothing");
	endlessSetFxPlayer(0);
	coopEndlessMode = false;

	// The shield refund uses the same perk stacks and never reaches the absorbed amount.
	qa_check(endlessPerkDeflectShieldSpared(10) == 3 && endlessPerkDeflectShieldSpared(100) == 34,
	         "two stacks spare 34% of what the shield stopped");
	endlessPerkTakenBy[0][PERK_DEFLECTOR] = 1;
	endlessPerkRederive();
	qa_check(endlessPerkDeflectShieldSpared(10) == 2 && endlessPerkDeflectShieldSpared(2) == 0,
	         "one stack spares 17%, in whole points, and a hit too small to discount spares none");
	bool overSpared = false;
	for (int hit = 1; hit <= 255; ++hit)
		overSpared |= (endlessPerkDeflectShieldSpared(hit) >= hit);
	qa_check(!overSpared, "...and no hit ever leaves the shield more than it spent");
	endlessMode = false;
	qa_check(endlessPerkDeflectShieldSpared(10) == 0,
	         "...outside an endless run the shield spends it all");
	endlessMode = true;
	coopEndlessMode = true;
	endlessSetFxPlayer(1);
	qa_check(endlessPerkDeflectShieldSpared(10) == 0, "a co-op partner without the perk spares nothing");
	endlessSetFxPlayer(0);
	coopEndlessMode = false;
	endlessPerkTakenBy[0][PERK_DEFLECTOR] = 2;
	endlessPerkRederive();

	// The returned shot: player two's, at the bullet, velocity and acceleration reversed.
	memset(shotAvail, 0, sizeof(shotAvail));
	EnemyShotType incoming;
	memset(&incoming, 0, sizeof(incoming));
	incoming.sx = 120;
	incoming.sy = 90;
	incoming.sxm = 3;
	incoming.sym = 5;
	incoming.sxc = 1;
	incoming.syc = -1;
	incoming.sgr = 12;
	incoming.animate = 1;
	incoming.animax = 3;
	incoming.filter = ENDLESS_CHAMPION_FILTER;
	const JE_integer id = player_shot_create_deflected(&incoming, 12, 2);
	qa_check(id < MAX_PWEAPON && shotAvail[id] > 0, "a returned shot takes a pool slot");
	if (id < MAX_PWEAPON)
	{
		const PlayerShotDataType *s = &playerShotData[id];
		qa_check(s->shotX == 120 && s->shotY == 90 && s->shotXM == -3 && s->shotYM == -5
		         && s->shotXC == -1 && s->shotYC == 1,
		         "...leaving from the bullet along the reverse of its path");
		qa_check(s->playerNumber == 2 && s->shotDmg == 12 && s->shotGr == 12 && s->shotAni == 1
		         && s->shotAniMax == 3 && s->tint == ENDLESS_CHAMPION_FILTER,
		         "...as the hit ship's own shot, at the returned damage, in the bullet's look and tier");
		qa_check(s->aimAtEnemy == 0 && s->aimDelayMax == 0 && !s->shotComplicated && s->chainReaction == 0
		         && s->salvoBoost == 0 && s->pierceLock == 0,
		         "...unsteered, and untagged outside a salvo window");
	}

	/* A deflection inside a charged Opening Salvo window joins that volley, and the window it joins
	 * is the deflecting ship's own. */
	memset(shotAvail, 0, sizeof(shotAvail));
	endlessSalvoWindow[0] = ENDLESS_PERK_SALVO_WINDOW;
	endlessSalvoWindow[1] = 0;
	coopEndlessMode = true;
	const JE_integer inSalvo = player_shot_create_deflected(&incoming, 12, 1);
	qa_check(inSalvo < MAX_PWEAPON && playerShotData[inSalvo].salvoBoost == 1,
	         "a deflection inside a charged window carries the volley's tag");
	const JE_integer partnerShot = player_shot_create_deflected(&incoming, 12, 2);
	qa_check(partnerShot < MAX_PWEAPON && playerShotData[partnerShot].salvoBoost == 0,
	         "...and the partner, whose own window is not running, deflects untagged");
	coopEndlessMode = false;
	memset(endlessSalvoWindow, 0, sizeof(endlessSalvoWindow));
	endlessSetFxPlayer(0);
	// The slot recycled by a gun's shot sheds the tint; the ordinary create keeps its own colours.
	memset(shotAvail, 0, sizeof(shotAvail));
	const uint savedPower = power;
	power = 10000;
	const JE_integer plain = player_shot_create(1, SHOT_FRONT, 100, 150, 0, 0, 1, 1);
	qa_check(plain < MAX_PWEAPON && playerShotData[plain].tint == 0,
	         "a gun's shot in the same slot flies untinted");
	power = savedPower;
	memset(shotAvail, 0, sizeof(shotAvail));

	incoming.sxm = 0;
	incoming.sym = 0;
	incoming.animax = 0;
	const JE_integer still = player_shot_create_deflected(&incoming, 3, 1);
	qa_check(still < MAX_PWEAPON && playerShotData[still].shotYM < 0 && playerShotData[still].shotAniMax == 1,
	         "a bullet that had stopped leaves straight up, and a still sprite stays on its frame");
	incoming.sgr = 65535;
	qa_check(player_shot_create_deflected(&incoming, 3, 1) == MAX_PWEAPON
	         && player_shot_create_deflected(&incoming, 0, 1) == MAX_PWEAPON,
	         "no shot for a frame past the sheets or for no damage");
	memset(shotAvail, 0, sizeof(shotAvail));

	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	memcpy(endlessSalvoWindow, savedSalvo, sizeof(savedSalvo));
	endlessPerkRederive();
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
}

static void qa_exec_bullet(int slot, int x, int y, JE_byte damage, JE_word graphic)
{
	memset(&enemyShot[slot], 0, sizeof(enemyShot[slot]));
	enemyShotAvail[slot] = false;
	enemyShot[slot].sx = (JE_integer)x;
	enemyShot[slot].sy = (JE_integer)y;
	enemyShot[slot].sxm = 1;
	enemyShot[slot].sym = 2;
	enemyShot[slot].sdmg = damage;
	enemyShot[slot].sgr = graphic;
}

static void qa_exec_clear(void)
{
	memset(enemyShotAvail, 1, sizeof(enemyShotAvail));
	memset(shotAvail, 0, sizeof(shotAvail));
}

static int qa_exec_returned(JE_byte owner, int *damage)
{
	int shots = 0;
	*damage = 0;
	for (int i = 0; i < MAX_PWEAPON; ++i)
	{
		if (shotAvail[i] == 0 || playerShotData[i].playerNumber != owner)
			continue;
		++shots;
		*damage += playerShotData[i].shotDmg;
	}
	return shots;
}

static int qa_exec_damage_at(int x, int y)
{
	for (int i = 0; i < MAX_PWEAPON; ++i)
		if (shotAvail[i] != 0 && playerShotData[i].shotX == x && playerShotData[i].shotY == y)
			return playerShotData[i].shotDmg;
	return -1;
}

static void qa_exec_stacks(uint seat, JE_byte stacks)
{
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkTakenBy[seat][PERK_EXECUTIONER] = stacks;
	endlessPerkRederive();
}

void qa_test_executioner_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode, savedNet = isNetworkGame;
	const bool savedHost = network_is_host;
	const uint savedThis = thisPlayerNum;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	static EnemyShotType savedShots[ENEMY_SHOT_MAX];
	static JE_boolean savedShotAvail[ENEMY_SHOT_MAX];
	memcpy(savedShots, enemyShot, sizeof(savedShots));
	memcpy(savedShotAvail, enemyShotAvail, sizeof(savedShotAvail));
	const uint slot = 40;
	const struct JE_SingleEnemyType savedEnemy = enemy[slot];

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	qa_exec_stacks(0, 0);
	endlessSetFxPlayer(0);

	qa_check(endlessPerkExecutionerBonus(100, 10, 100, false) == 0,
	         "without Executioner a finishing hit gains nothing");
	bool bonus = true;
	for (JE_byte stacks = 1; stacks <= 3; ++stacks)
	{
		qa_exec_stacks(0, stacks);
		bonus = bonus && endlessPerkExecutionerBonus(100, 24, 100, false) == 17 * stacks &&
		        endlessPerkExecutionerBonus(100, 25, 100, false) == 0 &&
		        endlessPerkExecutionerBonus(100, 19, 100, true) == 17 * stacks &&
		        endlessPerkExecutionerBonus(100, 20, 100, true) == 0;
	}
	qa_check(bonus,
	         "Executioner adds 17% per stack below 25% health, and against bosses below 20%");

	int radius = 0, count = 0, cap = 0;
	qa_exec_stacks(0, 0);
	qa_check(!endlessPerkExecutionReflect(&radius, &count, &cap),
	         "without the perk an execution reflects nothing");
	static const int reach[3][3] = { { 30, 6, 33 }, { 50, 13, 66 }, { 70, 20, 100 } };
	bool scaled = true;
	for (JE_byte stacks = 1; stacks <= 3; ++stacks)
	{
		qa_exec_stacks(0, stacks);
		scaled = scaled && endlessPerkExecutionReflect(&radius, &count, &cap) &&
		         radius == reach[stacks - 1][0] && count == reach[stacks - 1][1] &&
		         cap == reach[stacks - 1][2];
	}
	qa_check(scaled, "1, 2 and 3 stacks reflect 6, 13 and 20 bullets within 30, 50 and 70 px, "
	                 "each returning at most 33, 66 and 100 damage");

	qa_check(endlessReturnedShotDamage(300, 249) == 249 &&
	         endlessReturnedShotDamage(99, 249) == 100 && endlessReturnedShotDamage(99, 99) == 98 &&
	         endlessReturnedShotDamage(50, 30) == 30 && endlessReturnedShotDamage(7, 40) == 7,
	         "returned damage keeps clear of the ice and piercing markers and inside its cap");

	enemy[slot].ex = 100;
	enemy[slot].ey = 100;
	enemy[slot].mapoffset = 0;

	qa_exec_stacks(0, 1);
	qa_exec_clear();
	for (int i = 0; i < 7; ++i)
		qa_exec_bullet(i, 100 + 4 * (i + 1), 100, 5, 12);
	qa_exec_bullet(7, 131, 100, 5, 12);
	qa_exec_bullet(8, 104, 103, 5, 65535);
	qa_exec_bullet(9, 103, 104, 0, 12);
	execution_reflect_capture(slot, 0);
	bool took = true;
	for (int i = 0; i < 6; ++i)
		took = took && enemyShotAvail[i];
	const bool left = !enemyShotAvail[6] && !enemyShotAvail[7] && !enemyShotAvail[8] &&
	                  !enemyShotAvail[9];
	execution_reflect_process();
	int returned = 0;
	const int shots = qa_exec_returned(1, &returned);
	bool reversed = false;
	for (int i = 0; i < MAX_PWEAPON; ++i)
		reversed |= shotAvail[i] != 0 && playerShotData[i].shotX == 104 &&
		            playerShotData[i].shotY == 100 && playerShotData[i].shotXM == -1 &&
		            playerShotData[i].shotYM == -2;
	qa_check(took && left && shots == 6 && returned == 30 && reversed,
	         "one stack turns the six nearest bullets within 30 px into the killer's shots");

	qa_exec_clear();
	qa_exec_bullet(0, 110, 100, 30, 12);
	qa_exec_bullet(1, 120, 100, 255, 12);
	qa_exec_bullet(2, 125, 100, 40, 12);
	execution_reflect_capture(slot, 0);
	const bool turned = enemyShotAvail[0] && enemyShotAvail[1] && enemyShotAvail[2];
	execution_reflect_process();
	qa_check(turned && qa_exec_damage_at(110, 100) == 30 && qa_exec_damage_at(120, 100) == 33 &&
	         qa_exec_damage_at(125, 100) == 33 && qa_exec_returned(1, &returned) == 3,
	         "...each keeping its own damage up to 33, so a 255-damage bullet comes back at 33");

	static const int capByStacks[3] = { 33, 66, 100 };
	bool capped = true;
	for (JE_byte stacks = 1; stacks <= 3; ++stacks)
	{
		qa_exec_stacks(0, stacks);
		qa_exec_clear();
		qa_exec_bullet(0, 105, 100, 255, 12);
		qa_exec_bullet(1, 112, 100, 5, 12);
		execution_reflect_capture(slot, 0);
		execution_reflect_process();
		capped = capped && qa_exec_damage_at(105, 100) == capByStacks[stacks - 1] &&
		         qa_exec_damage_at(112, 100) == 5;
	}
	qa_check(capped, "a bullet's returned damage is capped at 33, 66 and 100 by stacks, alone");

	qa_exec_stacks(0, 3);
	qa_exec_clear();
	qa_exec_bullet(0, 105, 100, 120, 12);
	for (int i = 1; i <= 19; ++i)
		qa_exec_bullet(i, 100 + 3 * (i + 2), 100, 5, 12);
	execution_reflect_capture(slot, 0);
	bool allTurned = true;
	for (int i = 0; i <= 19; ++i)
		allTurned = allTurned && enemyShotAvail[i];
	execution_reflect_process();
	qa_check(allTurned && qa_exec_returned(1, &returned) == 20 && returned == 100 + 19 * 5 &&
	         qa_exec_damage_at(105, 100) == 100 && qa_exec_damage_at(112, 100) == 5,
	         "a 120-damage bullet turns at the 100 cap beside 19 others, each at its own");

	qa_exec_stacks(0, 3);
	qa_exec_clear();
	for (int i = 0; i < 22; ++i)
		qa_exec_bullet(i, 100 + 3 * (i + 1), 100, 1, 12);
	qa_exec_bullet(22, 100, 171, 1, 12);
	execution_reflect_capture(slot, 0);
	bool most = !enemyShotAvail[20] && !enemyShotAvail[21] && !enemyShotAvail[22];
	for (int i = 0; i < 20; ++i)
		most = most && enemyShotAvail[i];
	execution_reflect_process();
	qa_check(most && qa_exec_returned(1, &returned) == 20,
	         "three stacks take the 20 nearest of a wider volley, out to 70 px");

	qa_exec_stacks(0, 2);
	qa_exec_clear();
	qa_exec_bullet(0, 100, 150, 1, 12);
	qa_exec_bullet(1, 100, 151, 1, 12);
	qa_exec_bullet(3, 90, 100, 1, 12);
	qa_exec_bullet(4, 100, 90, 1, 12);
	qa_exec_bullet(5, 100, 107, 1, 12);
	qa_exec_bullet(7, 110, 100, 1, 12);
	qa_exec_bullet(8, 107, 100, 1, 12);
	execution_reflect_capture(slot, 0);
	const bool reach50 = enemyShotAvail[0] && !enemyShotAvail[1] && enemyShotAvail[3] &&
	                     enemyShotAvail[4] && enemyShotAvail[5] && enemyShotAvail[7] &&
	                     enemyShotAvail[8];
	execution_reflect_process();
	qa_check(reach50, "two stacks reach a bullet 50 px out but not one at 51");

	qa_exec_stacks(0, 1);
	qa_exec_clear();
	static const int ring[8][3] = {
		{ 7, 110, 100 }, { 3, 90, 100 }, { 9, 100, 110 }, { 4, 100, 90 },
		{ 12, 106, 108 }, { 1, 94, 108 }, { 15, 106, 92 }, { 6, 94, 92 },
	};
	for (unsigned i = 0; i < COUNTOF(ring); ++i)
		qa_exec_bullet(ring[i][0], ring[i][1], ring[i][2], 1, 12);
	execution_reflect_capture(slot, 0);
	const bool ties = enemyShotAvail[1] && enemyShotAvail[3] && enemyShotAvail[4] &&
	                  enemyShotAvail[6] && enemyShotAvail[7] && enemyShotAvail[9] &&
	                  !enemyShotAvail[12] && !enemyShotAvail[15];
	execution_reflect_process();
	qa_check(ties, "among equally near bullets the earlier pool slots go first");

	coopEndlessMode = true;
	qa_exec_stacks(1, 2);
	endlessSetFxPlayer(0);
	qa_exec_clear();
	qa_exec_bullet(0, 110, 100, 5, 12);
	execution_reflect_capture(slot, 0);
	const bool bareSeat = !enemyShotAvail[0];
	execution_reflect_capture(slot, 1);
	const bool holder = enemyShotAvail[0] && endlessFxPlayer() == 0;
	execution_reflect_process();
	qa_check(bareSeat && holder && qa_exec_returned(2, &returned) == 1 && returned == 5 &&
	         qa_exec_returned(1, &returned) == 0 && endlessFxPlayer() == 0,
	         "in co-op the killer's own stacks decide, the reflections are theirs, and the "
	         "ambient effect player is left alone");

	Uint32 parity[4];
	for (uint m = 0; m < 4; ++m)
	{
		isNetworkGame = m != 0;
		thisPlayerNum = m == 2 ? 2 : 1;
		network_is_host = m == 1;
		endlessSetFxPlayer(m % 2);
		qa_exec_stacks(m % 2 == 0 ? 1 : 0, 3);
		qa_exec_clear();
		for (int i = 0; i < 16; ++i)
			qa_exec_bullet(i * 3, 60 + (i * 37) % 90, 50 + (i * 53) % 110,
			               (JE_byte)(1 + (i * 7) % 23), 12);
		execution_reflect_capture(slot, m % 2 == 0 ? 1 : 0);
		execution_reflect_process();
		Uint32 h = 0;
		for (int i = 0; i < 48; ++i)
			h = h * 31 + (enemyShotAvail[i] ? 1u : 0u);
		for (int i = 0; i < MAX_PWEAPON; ++i)
			if (shotAvail[i] != 0)
				h = h * 31 + (Uint32)(playerShotData[i].shotX * 1000 + playerShotData[i].shotY) +
				    playerShotData[i].shotDmg;
		parity[m] = h;
	}
	qa_check(parity[1] == parity[0] && parity[2] == parity[0] && parity[3] == parity[0],
	         "both machines, online or local, reflect the same bullets for either seat");

	isNetworkGame = savedNet;
	network_is_host = savedHost;
	thisPlayerNum = savedThis;
	endlessSetFxPlayer(0);
	enemy[slot] = savedEnemy;
	memcpy(enemyShot, savedShots, sizeof(savedShots));
	memcpy(enemyShotAvail, savedShotAvail, sizeof(savedShotAvail));
	memset(shotAvail, 0, sizeof(shotAvail));
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
}

void qa_test_executioner_every_kill(void)
{
	static JE_MultiEnemyType savedEnemies;
	static EnemyShotType savedShots[ENEMY_SHOT_MAX];
	static JE_boolean savedShotAvail[ENEMY_SHOT_MAX];
	JE_EnemyAvailType savedAvail;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedEnemies, enemy, sizeof(enemy));
	memcpy(savedAvail, enemyAvail, sizeof(enemyAvail));
	memcpy(savedShots, enemyShot, sizeof(savedShots));
	memcpy(savedShotAvail, enemyShotAvail, sizeof(savedShotAvail));
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	const Uint64 savedMods = endlessActiveMods;
	const JE_word savedKilled = enemyKilled;
	const int savedRunKills = endlessRunKills;
	const Player savedShip = player[0];
	const bool savedRamming = reworkEnabled[REWORK_RAMMING];
	const JE_byte savedRate = damageRate, savedNoclip = noclipMode;

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	endlessActiveMods = 0;
	endlessSetFxPlayer(0);
	qa_exec_stacks(0, 1);
	chain_reset_queue();

	static const struct
	{
		JE_boolean seen;
		JE_byte armor;
	} kills[] = { { false, 100 }, { true, 80 }, { true, 10 } };
	int returned = 0;
	bool everyKill = true;
	for (unsigned k = 0; k < COUNTOF(kills); ++k)
	{
		qa_enemy_clear_all();
		qa_exec_clear();
		qa_enemy_stage(0, 100, 100, kills[k].armor);
		enemy[0].healthbar_seen = kills[k].seen;
		enemy[0].healthbar_max = 100;
		qa_exec_bullet(0, 110, 100, 5, 12);
		enemy_kill_group(0, 0, 0);
		const bool taken = enemyShotAvail[0];
		execution_reflect_process();
		everyKill = everyKill && taken && qa_exec_returned(1, &returned) == 1 && returned == 5;
	}
	qa_check(everyKill, "a kill reflects whether it lands from full health, from above the "
	                    "wounded line or below it");

	qa_enemy_clear_all();
	qa_exec_clear();
	qa_enemy_stage(0, 100, 100, 1);
	qa_exec_bullet(0, 110, 100, 5, 12);
	enemy_kill_group(0, 0, ENDLESS_KILLER_NONE);
	const bool uncredited = !enemyShotAvail[0];
	coopEndlessMode = true;
	qa_exec_stacks(0, 1);
	qa_enemy_stage(1, 100, 100, 1);
	enemy_kill_group(1, 1, 1);
	const bool partner = !enemyShotAvail[0];
	coopEndlessMode = false;
	qa_exec_stacks(0, 1);
	execution_reflect_process();
	qa_check(uncredited && partner && qa_exec_returned(1, &returned) == 0 &&
	         qa_exec_returned(2, &returned) == 0,
	         "a kill credited to nobody, or to a partner without the perk, reflects nothing");

	qa_enemy_clear_all();
	qa_exec_clear();
	endlessPerkTakenBy[0][PERK_CHAINRXN] = 1;
	endlessPerkRederive();
	qa_enemy_stage(0, 100, 100, 1);
	qa_enemy_stage(1, 145, 100, 1);
	qa_exec_bullet(0, 105, 100, 5, 12);
	qa_exec_bullet(1, 150, 100, 5, 12);
	enemy_kill_group(0, 0, 0);
	const bool firstKill = enemyShotAvail[0] && !enemyShotAvail[1];
	chain_reaction_process();
	const bool chainKill = enemyAvail[1] == 1 && enemyShotAvail[1];
	execution_reflect_process();
	chain_reset_queue();
	qa_check(firstKill && chainKill && qa_exec_returned(1, &returned) == 2,
	         "a Chain Reaction kill reflects the fire near the enemy it takes");

	qa_enemy_clear_all();
	qa_exec_clear();
	qa_exec_stacks(0, 1);
	reworkEnabled[REWORK_RAMMING] = true;
	damageRate = 10;
	noclipMode = 0;
	qa_enemy_stage(0, 100, 150, 1);
	player[0].x = 100;
	player[0].y = 150;
	player[0].is_alive = true;
	player[0].armor = 28;
	player[0].invulnerable_ticks = 0;
	qa_exec_bullet(0, 110, 150, 5, 12);
	JE_playerCollide(&player[0], 1);
	const bool rammed = enemyAvail[0] == 1 && enemyShotAvail[0];
	execution_reflect_process();
	qa_check(rammed && qa_exec_returned(1, &returned) == 1,
	         "a ram kill reflects the fire near the enemy it takes");

	chain_reset_queue();
	JE_resetSP();
	memset(explosions, 0, sizeof(explosions));
	enemyKilled = savedKilled;
	endlessRunKills = savedRunKills;
	noclipMode = savedNoclip;
	damageRate = savedRate;
	reworkEnabled[REWORK_RAMMING] = savedRamming;
	player[0] = savedShip;
	endlessActiveMods = savedMods;
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	memcpy(enemyShot, savedShots, sizeof(savedShots));
	memcpy(enemyShotAvail, savedShotAvail, sizeof(savedShotAvail));
	memset(shotAvail, 0, sizeof(shotAvail));
	memcpy(enemy, savedEnemies, sizeof(enemy));
	memcpy(enemyAvail, savedAvail, sizeof(enemyAvail));
	endlessSetFxPlayer(0);
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
}

static bool qa_afterburner_close(float got, float want)
{
	return got > want - 0.01f && got < want + 0.01f;
}

static float qa_afterburner_full_boost(void)
{
	const int stacks = endlessPerkMaxStack(PERK_AFTERBURNER);
	return 1.0f + (float)(stacks * ENDLESS_PERK_AFTERBURNER_SPEED_PCT) / 100.0f;
}

static float qa_afterburner_full_resist(void)
{
	const int stacks = endlessPerkMaxStack(PERK_AFTERBURNER);
	return (float)endlessClamp(stacks * ENDLESS_PERK_AFTERBURNER_RESIST_PCT, 0,
	                           ENDLESS_PERK_AFTERBURNER_RESIST_MAX) / 100.0f;
}

static float qa_afterburner_ladder_top(uint p, float floor, bool *rising)
{
	float previous = floor;
	*rising = true;
	for (int stacks = 1; stacks <= endlessPerkMaxStack(PERK_AFTERBURNER); ++stacks)
	{
		endlessPerkSetOwnedFor(p, PERK_AFTERBURNER, stacks);
		const float scale = endlessShipMoveScale(p);
		*rising = *rising && scale > previous;
		previous = scale;
	}
	return previous;
}

static int qa_afterburner_pull(int stacks, bool sideways)
{
	enum { PULL_TICKS = 100 };
	endlessPerkSetOwnedFor(0, PERK_AFTERBURNER, stacks);
	endlessRollGravityDir();
	int pulled = 0;
	for (int tick = 0; tick < PULL_TICKS; ++tick)
		pulled += sideways ? endlessGravityPullX(0) : endlessGravityPullY(0);
	return pulled;
}

static bool qa_afterburner_pulled(int plain, int resisted, float resist)
{
	const int want = (int)((float)abs(plain) * (1.0f - resist) + 0.5f);
	return plain != 0 && (resisted < 0) == (plain < 0) && abs(resisted) >= want - 1 &&
	       abs(resisted) <= want + 1;
}

static Uint64 qa_sideways_gravity_seed(void)
{
	Uint64 heading = 0;
	do
	{
		endlessReseed(heading);
		endlessRollGravityDir();
	} while (fabsf(endlessGravityDriftX()) < 0.5f * endlessGravityDrift() && ++heading < 64);
	return heading;
}

void qa_test_afterburner_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	const Uint64 savedMods = endlessActiveMods;
	const int savedDepth = endlessRunDepth;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	endlessActiveMods = 0;
	endlessRunDepth = 40;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();

	qa_check(endlessShipMoveScale(0) == 1.0f && endlessShipMoveScale(1) == 1.0f,
	         "without Afterburners or Molasses both ships move at stock speed");

	const int maxStacks = endlessPerkMaxStack(PERK_AFTERBURNER);
	const float fullBoost = qa_afterburner_full_boost();
	const float fullResist = qa_afterburner_full_resist();
	bool rising;
	float top = qa_afterburner_ladder_top(0, 1.0f, &rising);
	qa_check(maxStacks == 5 && rising && qa_afterburner_close(top, fullBoost),
	         "each Afterburners stack adds speed, reaching the full boost at five");

	endlessActiveMods = ENDLESS_MOD_SLUGGISH;
	const float molasses = endlessMoveScale();
	endlessPerkSetOwnedFor(0, PERK_AFTERBURNER, 0);
	const bool stockSlowed = molasses < 1.0f && endlessShipMoveScale(0) == molasses;
	top = qa_afterburner_ladder_top(0, molasses, &rising);
	const float resisted = (1.0f - (1.0f - molasses) * (1.0f - fullResist)) * fullBoost;
	qa_check(stockSlowed && rising && qa_afterburner_close(top, resisted) && top < fullBoost,
	         "Afterburners shave Molasses a stack at a time, never all of it, and add their speed");

	endlessActiveMods = ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_GRAVITY;
	endlessPerkSetOwnedFor(0, PERK_AFTERBURNER, 0);
	const float pull = endlessGravityDrift();
	const bool stockPull = pull > 0.0f && endlessShipGravityDriftY(0) == pull;
	endlessPerkSetOwnedFor(0, PERK_AFTERBURNER, maxStacks);
	const float resistedPull = endlessShipGravityDriftY(0);
	qa_check(stockPull && endlessGravityDrift() == pull && endlessMoveScale() == molasses &&
	         resistedPull > 0.0f && qa_afterburner_close(resistedPull, pull * (1.0f - fullResist)),
	         "Afterburners resist only their own ship's gravity, and never fully");

	const int plainPulled = qa_afterburner_pull(0, false);
	const int resistedPulled = qa_afterburner_pull(maxStacks, false);
	qa_check(plainPulled > 0 && qa_afterburner_pulled(plainPulled, resistedPulled, fullResist),
	         "Smooth-Motion-off gravity pulls a resisting ship the matching fraction of the way");

	endlessActiveMods |= ENDLESS_MOD_GRAVITY_OMNI;
	const Uint64 heading = qa_sideways_gravity_seed();
	endlessReseed(heading);
	const int plainSideways = qa_afterburner_pull(0, true);
	endlessReseed(heading);
	const int resistedSideways = qa_afterburner_pull(maxStacks, true);
	qa_check(qa_afterburner_pulled(plainSideways, resistedSideways, fullResist),
	         "...and a Rogue Well heading's sideways pull by the same fraction");
	endlessActiveMods &= ~ENDLESS_MOD_GRAVITY_OMNI;
	endlessRollGravityDir();

	endlessActiveMods = ENDLESS_MOD_GRAVITY;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkSetOwnedFor(1, PERK_AFTERBURNER, 2);
	coopEndlessMode = true;
	const bool coopOwnShip = endlessShipMoveScale(0) == 1.0f && endlessShipMoveScale(1) > 1.0f &&
	                         endlessShipGravityDriftY(1) < endlessShipGravityDriftY(0);
	coopEndlessMode = false;
	const bool soloIgnoresRowTwo = endlessShipMoveScale(1) == 1.0f;
	endlessPerkSetOwnedFor(0, PERK_AFTERBURNER, 3);
	qa_check(coopOwnShip && soloIgnoresRowTwo && endlessShipMoveScale(1) > 1.0f,
	         "Afterburners act only on their own ship in co-op and on the shared row elsewhere");

	endlessMode = false;
	qa_check(endlessShipMoveScale(0) == 1.0f && endlessShipGravityDriftY(0) == 0.0f,
	         "Afterburners do nothing outside Endless effects");

	qa_check(player_ship_move_scale_self_test(),
	         "Smooth-Motion-off movement scales the whole step, momentum included");
	qa_check(player_ship_move_carry_reset_self_test(),
	         "a level start clears both seats' Smooth-Motion-off movement and mouse carries");

	endlessActiveMods = 0;
	endlessRollGravityDir();

	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
	endlessActiveMods = savedMods;
	endlessRunDepth = savedDepth;
}

static bool qa_concussed_by_hits(uint p, int hits)
{
	endlessSetFxPlayer(p);
	for (int hit = 0; hit < hits; ++hit)
		endlessConcussionHit();
	return endlessConcussionSlowdown(p) > 0.0f;
}

static bool qa_concussed_by_damage(int hits)
{
	const int savedShieldFlash[2] = { shieldGaugeFlash[0], shieldGaugeFlash[1] };
	const int savedArmorFlash[2] = { armorGaugeFlash[0], armorGaugeFlash[1] };
	const Player saved = player[0];
	bool concussed = false;
	for (int hit = 0; hit < hits && !concussed; ++hit)
	{
		player[0].shield = 50;
		player[0].shield_max = 50;
		player[0].armor = 50;
		player[0].is_alive = true;
		JE_playerDamage(1, &player[0]);
		concussed = endlessConcussionSlowdown(0) > 0.0f;
	}
	player[0] = saved;
	memset(explosions, 0, sizeof(explosions));
	shieldGaugeFlash[0] = savedShieldFlash[0]; shieldGaugeFlash[1] = savedShieldFlash[1];
	armorGaugeFlash[0] = savedArmorFlash[0];   armorGaugeFlash[1] = savedArmorFlash[1];
	return concussed;
}

void qa_test_concussion_mod(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	const Uint64 savedMods = endlessActiveMods;
	const int savedDepth = endlessRunDepth;
	const JE_shortint savedDifficulty = difficultyLevel;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	qa_rng_save();

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	difficultyLevel = DIFFICULTY_NORMAL;
	endlessRunDepth = 0;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	mt_srand(0xC0C0551u);

	endlessActiveMods = 0;
	endlessConcussionReset();
	qa_check(!qa_concussed_by_hits(0, 200) && endlessShipMoveScale(0) == 1.0f,
	         "without Concussion no hit slows the ship");

	endlessActiveMods = ENDLESS_MOD_CONCUSSION;
	const int trials = 1000;
	int stunned = 0;
	for (int trial = 0; trial < trials; ++trial)
	{
		endlessConcussionReset();
		stunned += qa_concussed_by_hits(0, 1) ? 1 : 0;
	}
	qa_check(stunned == trials, "every hit concusses a ready ship");

	endlessConcussionReset();
	const bool landed = qa_concussed_by_hits(0, 1);
	const float surfaceSlow = endlessConcussionSlowdown(0);
	const float surfaceScale = endlessShipMoveScale(0);
	int stunTicks = 0;
	while (qa_concussed_by_hits(0, 1) && stunTicks < 1000)
	{
		endlessConcussionTick();
		++stunTicks;
	}
	qa_check(landed && qa_afterburner_close(surfaceSlow, 0.50f) &&
	         qa_afterburner_close(surfaceScale, 0.50f) && stunTicks == ENDLESS_CONCUSSION_TICKS,
	         "a concussion halves the ship's speed for its whole stun");

	bool guarded = true;
	for (int tick = 0; tick < ENDLESS_CONCUSSION_GUARD_TICKS; ++tick)
	{
		guarded = guarded && !qa_concussed_by_hits(0, 8);
		endlessConcussionTick();
	}
	qa_check(guarded && qa_concussed_by_hits(0, 1),
	         "...and no hit concusses it again until the guard after the stun runs out");

	endlessRunDepth = 400;
	difficultyLevel = DIFFICULTY_IMPOSSIBLE;
	endlessConcussionReset();
	const bool deepLanded = qa_concussed_by_hits(0, 1);
	const float deepSlow = endlessConcussionSlowdown(0);
	int deepTicks = 0;
	while (qa_concussed_by_hits(0, 1) && deepTicks < 1000)
	{
		endlessConcussionTick();
		++deepTicks;
	}
	endlessRunDepth = 0;
	difficultyLevel = DIFFICULTY_NORMAL;
	qa_check(deepLanded && deepSlow == surfaceSlow && deepTicks == stunTicks,
	         "a concussion is as strong and as long at any depth or difficulty");

	endlessConcussionReset();
	qa_concussed_by_hits(0, 1);
	bool rising;
	const float top = qa_afterburner_ladder_top(0, surfaceScale, &rising);
	const float resisted = (1.0f - surfaceSlow * (1.0f - qa_afterburner_full_resist()))
	                     * qa_afterburner_full_boost();
	qa_check(rising && qa_afterburner_close(top, resisted),
	         "Afterburners shave a concussion a stack at a time, as they do Molasses");
	endlessPerkSetOwnedFor(0, PERK_AFTERBURNER, 0);

	endlessActiveMods = ENDLESS_MOD_CONCUSSION | ENDLESS_MOD_SLUGGISH;
	const float molasses = endlessMoveScale();
	const float stacked = endlessShipMoveScale(0);
	qa_check(molasses < 1.0f && qa_afterburner_close(stacked, molasses * surfaceScale),
	         "a concussion under Molasses multiplies the two slowdowns");

	endlessActiveMods = ENDLESS_MOD_CONCUSSION;
	coopEndlessMode = true;
	endlessConcussionReset();
	const bool partnerHit = qa_concussed_by_hits(1, 1);
	qa_check(partnerHit && endlessConcussionSlowdown(0) == 0.0f && endlessShipMoveScale(0) == 1.0f,
	         "a concussion slows only the ship that took the hit");
	endlessResetZoneEffects();
	qa_check(endlessConcussionSlowdown(1) == 0.0f && qa_concussed_by_hits(1, 1),
	         "a zone start clears every ship's concussion and its guard");
	coopEndlessMode = false;

	endlessConcussionReset();
	qa_check(qa_concussed_by_damage(1),
	         "a real hit through the damage path concusses the ship it lands on");
	qa_check(endlessStripWorstMod(ENDLESS_MOD_CONCUSSION | ENDLESS_MOD_SWIFT) == ENDLESS_MOD_SWIFT,
	         "Sector Sabotage strips Concussion before Swift Death");
	qa_check(endlessModWord(ENDLESS_MOD_CONCUSSION)[0] != '\0' &&
	         endlessDangerScore(ENDLESS_MOD_CONCUSSION) > 0,
	         "Concussion lists a monitor row and counts as a danger");

	endlessConcussionReset();
	endlessMode = false;
	qa_check(!qa_concussed_by_hits(0, 200) && endlessShipMoveScale(0) == 1.0f,
	         "Concussion does nothing outside Endless effects");

	endlessConcussionReset();
	endlessSetFxPlayer(0);
	qa_rng_restore();
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
	endlessActiveMods = savedMods;
	endlessRunDepth = savedDepth;
	difficultyLevel = savedDifficulty;
}

void qa_test_boss_scroll_hold(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const Uint64 savedMods = endlessActiveMods;
	const boss_bar_t savedBars[2] = { boss_bar[0], boss_bar[1] };

	endlessMode = true;
	endlessCampaignMods = false;
	endlessActiveMods = ENDLESS_MOD_WARP;
	boss_bar[0].link_num = 0;
	boss_bar[1].link_num = 0;
	const int openPace = endlessScrollBoostPercent();
	boss_bar[1].link_num = 62;
	const int heldPace = endlessScrollBoostPercent();
	const bool debtKept = enemy_approach_debt_hold_self_test();
	boss_bar[1].link_num = 0;
	const int resumedPace = endlessScrollBoostPercent();

	boss_bar[0] = savedBars[0];
	boss_bar[1] = savedBars[1];
	endlessActiveMods = savedMods;
	endlessCampaignMods = savedCampaign;
	endlessMode = savedEndless;

	qa_check(openPace == 220 && heldPace == 0 && resumedPace == 220,
	         "a boss bar holds a boosted scroll at the authored pace until no bar is up");
	qa_check(debtKept, "...and the hold keeps every enemy's banked approach debt for its settle");
}

static void qa_pull_place(int x, int y)
{
	player[0].x = x;
	player[0].y = y;
	player[0].x_velocity = 0;
	player[0].y_velocity = 0;
	player[0].is_alive = false;
	vt_ship_step(1.0f);
	player[0].is_alive = true;
}

static void qa_pull_ticks(int ticks, float frame, float hitch, bool *still, unsigned *seen,
                          int *moved, int *pulled)
{
	float acc = 0.0f;
	int prevX = player[0].x, prevY = player[0].y;
	for (int t = 0, f = 0; t < ticks; ++f)
	{
		const float dt = (hitch > 0.0f && f % 3 == 1) ? hitch : frame;
		vt_ship_step(dt);
		acc += dt;
		if (acc < 1.0f)
			continue;
		acc = MIN(acc - 1.0f, 1.0f);

		int pullX, pullY;
		vt_ship_take_pull(0, &pullX, &pullY);
		const int dvx = player[0].x - prevX, dvy = player[0].y - prevY;
		prevX = player[0].x;
		prevY = player[0].y;
		*still = *still && abs(dvx - pullX) <= 1 && abs(dvy - pullY) <= 1 &&
		         player_ship_banking(true, true, player[0].x_velocity, dvx, pullX) == 0;
		*seen |= 1u << (player_ship_banking(true, true, player[0].x_velocity, dvx, 0) + 2);
		*moved += dvx;
		*pulled += pullX;
		++t;
	}
}

void qa_test_gravity_pull_banking(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode, savedTwo = twoPlayerMode;
	const Uint64 savedMods = endlessActiveMods;
	const int savedDepth = endlessRunDepth, savedJoysticks = joysticks;
	const JE_shortint savedDifficulty = difficultyLevel;
	const bool savedMouse = has_mouse;
	const Player savedPlayer = player[0];
	static Uint8 savedKeys[SDL_NUM_SCANCODES];
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedKeys, keysactive, sizeof(savedKeys));
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	twoPlayerMode = false;
	endlessRunDepth = 40;
	difficultyLevel = DIFFICULTY_NORMAL;
	joysticks = 0;
	has_mouse = false;
	memset(keysactive, 0, sizeof(keysactive));
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();

	const int midX = 150, midY = (SHIP_TOP_MARGIN + SHIP_BOTTOM_MARGIN) / 2;
	static const float pacing[][2] = {
		{ 35.0f / 60.0f, 0.0f }, { 35.0f / 144.0f, 0.0f }, { 0.35f, 1.6f },
	};

	endlessActiveMods = ENDLESS_MOD_GRAVITY | ENDLESS_MOD_GRAVITY_OMNI;
	endlessReseed(qa_sideways_gravity_seed());
	endlessRollGravityDir();
	const float driftX = endlessShipGravityDriftX(0);

	bool still = true;
	unsigned seen = 0;
	bool accounted = true;
	for (unsigned i = 0; i < COUNTOF(pacing); ++i)
	{
		qa_pull_place(midX, midY);
		int moved = 0, pulled = 0;
		qa_pull_ticks(20, pacing[i][0], pacing[i][1], &still, &seen, &moved, &pulled);
		accounted = accounted && moved != 0 && abs(moved - pulled) <= 1;
	}
	qa_check(fabsf(driftX) >= 1.5f && (seen & (seen - 1)) != 0,
	         "a Rogue Well's sideways pull moves the ship a frame-paced distance each tick");
	qa_check(still && accounted,
	         "...yet a ship the player leaves alone never banks from it, at any frame pacing");

	qa_pull_place(midX, midY);
	bool approach = true;
	unsigned approachSeen = 0;
	int approachMoved = 0, approachPulled = 0;
	qa_pull_ticks(120, pacing[0][0], 0.0f, &approach, &approachSeen,
	              &approachMoved, &approachPulled);
	const int wall = driftX > 0.0f ? PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN : SHIP_LEFT_MARGIN;
	bool pinned = player[0].x == wall;
	int pinnedMoved = 0, pinnedPulled = 0;
	qa_pull_ticks(30, pacing[0][0], 0.0f, &pinned, &approachSeen, &pinnedMoved, &pinnedPulled);
	qa_check(approach && pinned && pinnedMoved == 0 && pinnedPulled == 0,
	         "...and once it pins the ship to a wall, the ship neither moves nor banks against it");

	static const Uint64 inert[] = { ENDLESS_MOD_GRAVITY, 0 };
	bool level = true;
	for (unsigned i = 0; i < COUNTOF(inert); ++i)
	{
		endlessActiveMods = inert[i];
		endlessRollGravityDir();
		qa_pull_place(midX, midY);
		bool flat = true;
		unsigned flatSeen = 0;
		int moved = 0, pulled = 0;
		qa_pull_ticks(120, pacing[2][0], pacing[2][1], &flat, &flatSeen, &moved, &pulled);
		const int restY = inert[i] != 0 ? SHIP_BOTTOM_MARGIN : midY;
		level = level && flat && moved == 0 && pulled == 0 && player[0].y == restY;
	}
	qa_check(level,
	         "a downward or absent pull never banks the ship; the floor absorbs a downward one");

	qa_check(player_ship_banking(false, true, 0, 6, 6) == 0 &&
	         player_ship_banking(false, true, 0, 9, 3) == 1 &&
	         player_ship_banking(true, false, 4, 3, 3) == 0 &&
	         player_ship_banking(true, true, 4, 3, 3) == 2,
	         "every banking path drops the pull's share of the movement; momentum still banks");

	player[0].is_alive = false;
	vt_ship_step(1.0f);
	player[0] = savedPlayer;
	endlessActiveMods = 0;
	endlessRollGravityDir();

	memcpy(keysactive, savedKeys, sizeof(savedKeys));
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	has_mouse = savedMouse;
	joysticks = savedJoysticks;
	difficultyLevel = savedDifficulty;
	endlessRunDepth = savedDepth;
	endlessActiveMods = savedMods;
	twoPlayerMode = savedTwo;
	coopEndlessMode = savedCoop;
	endlessCampaignMods = savedCampaign;
	endlessMode = savedEndless;
}

/* Countermeasures clears shots within hull-relative reach and has no cooldown. */
void qa_test_countermeasure_burst(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	const Player savedPlayer = player[0];
	JE_byte savedPerks[2][PERK_COUNT];
	EnemyShotType savedShots[8];
	JE_boolean savedAvail[ENEMY_SHOT_MAX];
	Explosion savedExplosions[MAX_EXPLOSIONS];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	memcpy(savedShots, enemyShot, sizeof(savedShots));
	memcpy(savedAvail, enemyShotAvail, sizeof(savedAvail));
	memcpy(savedExplosions, explosions, sizeof(savedExplosions));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);

	player[0].x = 140;
	player[0].y = 100;
	player[0].shot_hit_area_x = 12;
	player[0].shot_hit_area_y = 10;
	const int reachX = 12 + ENDLESS_PERK_CM_RADIUS1;
	const int reachY = 10 + ENDLESS_PERK_CM_RADIUS1;

	// Check both axes because the hitbox is not square.
	memset(enemyShotAvail, 1, sizeof(enemyShotAvail));   // an empty sky, so the counts below are exact
	for (uint i = 0; i < COUNTOF(savedShots); ++i)
	{
		memset(&enemyShot[i], 0, sizeof(enemyShot[i]));
		enemyShotAvail[i] = 0;
		enemyShot[i].sx = 140;
		enemyShot[i].sy = 100;
		enemyShot[i].sgr = 270;
	}
	enemyShot[4].sx = (JE_integer)(140 + reachX);
	enemyShot[5].sx = (JE_integer)(140 + reachX + 1);
	enemyShot[6].sy = (JE_integer)(100 + reachY);
	enemyShot[7].sy = (JE_integer)(100 + reachY + 1);

	qa_check(endlessCountermeasureBurst(&player[0]) == 0 && enemyShotAvail[0] == 0,
	         "without the perk a hull hit sweeps nothing");

	endlessPerkTakenBy[0][PERK_COUNTERMEASURE] = 1;
	endlessPerkRederive();
	qa_check(endlessCountermeasureBurst(&player[0]) == 6,
	         "the burst takes every bullet out to its reach, measured past the hitbox");
	qa_check(enemyShotAvail[0] && enemyShotAvail[4] && enemyShotAvail[6]
	         && !enemyShotAvail[5] && !enemyShotAvail[7],
	         "...and leaves the ones a pixel beyond it, on either axis, flying");

	endlessPerkTakenBy[0][PERK_COUNTERMEASURE] = 2;
	endlessPerkRederive();
	qa_check(endlessCountermeasureBurst(&player[0]) == 2
	         && enemyShotAvail[5] && enemyShotAvail[7],
	         "a second stack reaches the bullets one stack left flying");
	qa_check(endlessCountermeasureBurst(&player[0]) == 0,
	         "a burst over cleared air takes nothing");

	// Each consecutive hit must clear a fresh shot without a recharge tick.
	endlessPerkTakenBy[0][PERK_COUNTERMEASURE] = 1;
	endlessPerkRederive();
	bool everyHit = true;
	for (uint hit = 0; hit < 8; ++hit)
	{
		enemyShotAvail[0] = 0;
		enemyShot[0].sx = 140;
		enemyShot[0].sy = 100;
		everyHit = everyHit && endlessCountermeasureBurst(&player[0]) == 1 && enemyShotAvail[0];
	}
	qa_check(everyHit, "consecutive hull hits each fire: nothing has to recharge between them");

	endlessMode = false;
	enemyShotAvail[0] = 0;
	qa_check(endlessCountermeasureBurst(&player[0]) == 0 && enemyShotAvail[0] == 0,
	         "...and none of it happens outside an endless run");

	memcpy(enemyShot, savedShots, sizeof(savedShots));
	memcpy(enemyShotAvail, savedAvail, sizeof(savedAvail));
	memcpy(explosions, savedExplosions, sizeof(savedExplosions));
	JE_resetSP();
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	player[0] = savedPlayer;
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
}

/* Guided Aim changes the homing x-coordinate, not the stock target eligibility rules. */
void qa_test_guided_screen_aim(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const bool savedGuidedAim = guidedShotScreenAim;
	const uint savedPower = power;

	endlessMode = false;   // no perk in the way: the weapon-table branch alone decides
	endlessCampaignMods = false;
	power = 10000;
	memset(shotAvail, 0, sizeof(shotAvail));

	const JE_word guidedGun = 587;  // Heavy Guided Bombs: one shot that homes on its own
	qa_check(weapons[guidedGun].aim > 5 && weapons[guidedGun].multi == 1,
	         "the Guided Aim test's gun is the one it assumes");
	const int guidedOwn = weapons[guidedGun].aim - 5;

	/* Two enemies level with each other, both right of the shot on the map: A is the nearer by
	 * map x, B by screen x. B is an invulnerable part, which the stock rule takes either way. */
	qa_enemy_clear_all();
	enemyAvail[0] = 0;
	enemy[0].ex = 110;
	enemy[0].mapoffset = 30;    // screen x 140
	enemy[0].ey = 100;
	enemy[0].armorleft = 20;
	enemyAvail[1] = 0;
	enemy[1].ex = 140;
	enemy[1].mapoffset = -40;   // screen x 100, level with the shot
	enemy[1].ey = 100;
	enemy[1].armorleft = 255;

	guidedShotScreenAim = false;
	JE_integer id = qa_guidance_fire(SHOT_FRONT, guidedGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 1
	         && playerShotData[id].aimDelayMax == guidedOwn,
	         "with Guided Aim off a guided shot picks the nearest enemy by map x");
	memset(shotAvail, 0, sizeof(shotAvail));

	guidedShotScreenAim = true;
	id = qa_guidance_fire(SHOT_FRONT, guidedGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 2
	         && playerShotData[id].aimDelayMax == guidedOwn,
	         "with Guided Aim on it picks by screen x, invulnerable part included, on its own interval");

	// The correction from one spot: B's map x is right of the shot, its screen x is on the shot.
	PlayerShotDataType *s = &playerShotData[id];
	s->shotX = 100;
	s->shotXM = 0;
	s->shotYM = -12;
	s->aimDelay = 1;
	player_shot_aim_step(s);
	qa_check(s->shotXM == -1 && s->shotYM == -13 && s->aimDelay == guidedOwn,
	         "a Guided Aim shot bends toward the enemy's screen x and reloads its interval");
	guidedShotScreenAim = false;
	s->shotXM = 0;
	player_shot_aim_step(s);
	qa_check(s->shotXM == 1, "...where the shipped rule bends toward its map x");

	// The enemy dies: the stock veer-off still applies, and no retarget happens.
	guidedShotScreenAim = true;
	enemyAvail[1] = 1;
	s->shotXM = -1;
	player_shot_aim_step(s);
	qa_check(s->aimAtEnemy == 2 && s->shotXM == -2,
	         "a Guided Aim shot whose enemy died veers off like the shipped rule");
	memset(shotAvail, 0, sizeof(shotAvail));

	qa_enemy_clear_all();
	guidedShotScreenAim = savedGuidedAim;
	power = savedPower;
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
}

/* Which enemies a tier roll may land on. Every body settles on its first frame, so the roll reads
 * the level script to tell a boss flying in armored from scenery that can never be hurt. */
void qa_test_elite_tier_eligibility(void)
{
	const bool savedMode = endlessMode, savedCampaign = endlessCampaignMods;
	const Uint64 savedMods = endlessActiveMods;
	const int savedDepth = endlessRunDepth;
	const JE_word savedMaxEvent = maxEvent;
	struct JE_EventRecType savedEvents[4];
	memcpy(savedEvents, eventRec, sizeof(savedEvents));

	endlessMode = true;
	endlessCampaignMods = false;
	endlessRunDepth = 20;
	endlessActiveMods = ENDLESS_MOD_APEX;  // every roll returns a tier

	// A level that opens link 64 with a damage event, seals link 12 with one, and renumbers
	// link 9 into 64. Link 3 is named nowhere, so nothing can ever hurt it.
	memset(eventRec, 0, sizeof(savedEvents));
	eventRec[0].eventtype = 25;
	eventRec[0].eventdat = 200;
	eventRec[0].eventdat4 = 64;
	eventRec[1].eventtype = 25;
	eventRec[1].eventdat = 255;
	eventRec[1].eventdat4 = 12;
	eventRec[2].eventtype = 39;
	eventRec[2].eventdat = 9;
	eventRec[2].eventdat2 = 64;
	maxEvent = 3;

	endlessResetElites();
	qa_check(endlessEliteTierNow(0, 40, false) >= 2, "a damageable enemy takes a tier");
	qa_check(endlessEliteTierNow(0, 255, true) == 1, "a score pickup is never a tier");
	qa_check(endlessEliteTierNow(3, 255, false) == 1, "scenery no armor event can reach is never promoted");
	qa_check(endlessEliteTierNow(12, 255, false) == 1, "...nor is a body an armor event only seals");

	endlessResetElites();
	const int boss = endlessEliteTierNow(64, 255, false);
	qa_check(boss >= 2, "a boss flying in armored takes its tier while it is still invulnerable");
	qa_check(endlessEliteTierNow(64, 254, false) == boss,
	         "...and keeps it when the level's damage event opens it up");
	qa_check(endlessEliteTierNow(9, 255, false) >= 2,
	         "a group renumbered into an opened one rolls too, under its own number");

	endlessResetElites();
	qa_check(endlessEliteTierNow(64, 254, false) == endlessEliteTierNow(64, 255, false),
	         "an invulnerable part wears the tier its link group already holds");

	/* A part the roll never reached still paints in its group's bank, so a hull that mixes a
	 * damageable core with sealed plating tints as one body. */
	const Uint8 groupTint = endlessEliteTint(endlessEliteTierNow(64, 254, false));
	qa_check(groupTint != 0 && endlessEliteShellTint(64, 255) == groupTint,
	         "sealed plating borrows the colour its link group holds");
	qa_check(endlessEliteShellTint(64, 100) == 0,
	         "...while a damageable part is left to paint from its own tier");
	qa_check(endlessEliteShellTint(0, 255) == 0 && endlessEliteShellTint(3, 255) == 0,
	         "an unlinked part, and one whose group never rolls, stay untinted");

	// Wrecks clear both direct tier tint and borrowed shell tint.
	const JE_byte savedAvail = enemyAvail[0];
	const struct JE_SingleEnemyType savedEnemy = enemy[0];

	enemyAvail[0] = 0;
	enemy[0].edamaged = false;
	enemy[0].eliteState = 3;
	enemy[0].linknum = 64;
	enemy[0].armorleft = 255;
	qa_check(enemy_body_tint(0) == ENDLESS_CHAMPION_FILTER,
	         "a live champion paints in its own bank");
	enemy[0].eliteState = 1;
	qa_check(enemy_body_tint(0) == groupTint,
	         "...and its sealed plating in the one the group lends");

	enemyAvail[0] = 2;
	enemy[0].edamaged = true;
	qa_check(enemy_is_wreck(0) && enemy_body_tint(0) == 0,
	         "a wreck stops borrowing the group's bank");
	enemy[0].eliteState = 3;
	qa_check(enemy_body_tint(0) == 0, "...and a champion's wreck drops the bank it wore itself");

	enemy[0] = savedEnemy;
	enemyAvail[0] = savedAvail;

	// A damage event with no link number reaches every body on the field.
	memset(eventRec, 0, sizeof(savedEvents));
	eventRec[0].eventtype = 47;
	eventRec[0].eventdat = 30;
	maxEvent = 1;
	endlessResetElites();
	qa_check(endlessEliteTierNow(0, 255, false) >= 2, "a level-wide damage event opens even unlinked bodies");

	endlessActiveMods = ENDLESS_MOD_NOELITE;
	endlessResetElites();
	qa_check(endlessEliteTierNow(7, 40, false) == 1 && endlessEliteTierNow(7, 255, false) == 1,
	         "No Elites decides normal, and the group's parts still agree with it");

	memcpy(eventRec, savedEvents, sizeof(savedEvents));
	maxEvent = savedMaxEvent;
	endlessMode = savedMode;
	endlessCampaignMods = savedCampaign;
	endlessActiveMods = savedMods;
	endlessRunDepth = savedDepth;
	endlessResetElites();
}

/* Homing eligibility for active, scripted, linked, and pickup bodies. */
void qa_test_homing_chaser_eligibility(void)
{
	const bool savedMode = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_word savedMaxEvent = maxEvent;
	struct JE_EventRecType savedEvents[2];
	memcpy(savedEvents, eventRec, sizeof(savedEvents));

	endlessMode = true;
	endlessCampaignMods = false;

	// Link 64 opens through an armor event; link 7 never opens.
	memset(eventRec, 0, sizeof(savedEvents));
	eventRec[0].eventtype = 25;
	eventRec[0].eventdat = 200;
	eventRec[0].eventdat4 = 64;
	maxEvent = 1;
	endlessResetElites();

	qa_check(endlessEnemyDestructible(0, 0, 40), "a damageable body can be shot");
	qa_check(!endlessEnemyDestructible(2, 0, 40),
	         "...but avail 2 carries no health the shot loop will look at");
	qa_check(endlessEnemyDestructible(0, 64, 255) && !endlessEnemyDestructible(0, 7, 255),
	         "a sealed body waits on its link group's damage event");

	// A destructible weak point shares link 12 with permanent scenery.
	const JE_byte savedAvail[3] = { enemyAvail[0], enemyAvail[1], enemyAvail[2] };
	struct JE_SingleEnemyType savedEnemy[3];
	memcpy(savedEnemy, enemy, sizeof(savedEnemy));

	memset(enemy, 0, sizeof(savedEnemy));
	enemyAvail[0] = 0;
	enemy[0].linknum = 12;
	enemy[0].armorleft = 40;
	enemyAvail[1] = 2;
	enemy[1].linknum = 12;
	enemy[1].armorleft = 255;
	enemyAvail[2] = 0;
	enemy[2].linknum = 0;
	enemy[2].armorleft = 40;
	endlessScanSceneryLinks();

	qa_check(!endlessHomingChaser(0) && !endlessHomingChaser(1),
	         "a span keeps still even where a destructible weak point sits inside it");
	qa_check(endlessHomingChaser(2), "an unlinked damageable body still gives chase");

	enemy[1].scoreitem = true;
	endlessScanSceneryLinks();
	qa_check(endlessHomingChaser(0),
	         "loot riding a link number is not scenery and poisons nothing");
	qa_check(!endlessHomingChaser(1), "...and the pickup itself never gives chase");

	// The tiers that floor tracking are also the ones whose wreckage would trail the ship.
	const Uint64 savedMods = endlessActiveMods;
	const Uint64 tiers[] = { ENDLESS_MOD_HOMING, ENDLESS_MOD_KAMIKAZE, ENDLESS_MOD_RAMPAGE };
	bool everyTier = true;
	for (unsigned t = 0; t < COUNTOF(tiers); ++t)
	{
		endlessActiveMods = tiers[t];
		everyTier = everyTier && endlessHomingTierActive();
	}
	endlessActiveMods = 0;
	qa_check(!endlessHomingTierActive(), "a sector with no homing tier keeps its wreckage");
	qa_check(everyTier, "...and every tier that floors tracking drops the corpse instead");
	endlessActiveMods = savedMods;

	memcpy(enemy, savedEnemy, sizeof(savedEnemy));
	for (unsigned i = 0; i < COUNTOF(savedAvail); ++i)
		enemyAvail[i] = savedAvail[i];

	memcpy(eventRec, savedEvents, sizeof(savedEvents));
	maxEvent = savedMaxEvent;
	endlessMode = savedMode;
	endlessCampaignMods = savedCampaign;
	endlessResetElites();
}

/* The tier tint an elite wears, and its route from a kill site into every explosion the death
 * spawns. Colour is presentation, so nothing here may reach the state hash. */
void qa_test_elite_explosion_tint(void)
{
	const bool savedMode = endlessMode, savedCampaign = endlessCampaignMods;

	endlessMode = false;
	endlessCampaignMods = false;
	qa_check(endlessEliteTint(2) == 0 && endlessEliteTint(3) == 0
	         && endlessEliteShellTint(64, 255) == 0,
	         "elite tints cannot leak into normal play, the one sealed plating borrows included");

	endlessMode = true;
	qa_check(endlessEliteTint(0) == 0 && endlessEliteTint(1) == 0
	         && endlessEliteTint(2) == ENDLESS_ELITE_FILTER
	         && endlessEliteTint(3) == ENDLESS_CHAMPION_FILTER,
	         "only elites and champions carry a tint, each its own bank");

	const Uint32 hashBefore = rollback_state_hash();
	memset(explosions, 0, sizeof(explosions));
	memset(rep_explosions, 0, sizeof(rep_explosions));

	explosionFilter = endlessEliteTint(3);
	JE_setupExplosion(100, 100, 0, 1, false, false);
	explosionFilter = 0;
	JE_setupExplosion(100, 100, 0, 1, false, false);
	qa_check(explosions[0].filter == ENDLESS_CHAMPION_FILTER && explosions[1].filter == 0,
	         "an explosion keeps the tint set when it was spawned");

	/* explonum 12 is over ten, so the death both puffs and queues a big repeating sequence. */
	memset(explosions, 0, sizeof(explosions));
	explosionFilter = endlessEliteTint(2);
	JE_setupExplosionLarge(false, 12, 100, 100);
	explosionFilter = 0;
	int tinted = 0;
	for (unsigned int i = 0; i < COUNTOF(explosions); ++i)
		if (explosions[i].ttl != 0 && explosions[i].filter == ENDLESS_ELITE_FILTER)
			++tinted;
	qa_check(tinted == 4, "all four puffs of one large explosion carry the tint");
	qa_check(rep_explosions[0].ttl != 0 && rep_explosions[0].filter == ENDLESS_ELITE_FILTER,
	         "...and the repeating sequence it arms carries it into its own explosions");

	memset(explosions, 0, sizeof(explosions));
	memset(rep_explosions, 0, sizeof(rep_explosions));
	qa_check(rollback_state_hash() == hashBefore,
	         "explosion colour leaves the registered state byte stream untouched");

	// Local opacity stays outside registered snapshot bytes.
	explosionOpacity = 8;
	JE_setupExplosion(100, 100, 0, 14, false, false);
	explosionOpacity = NET_STYLE_SOLID;
	JE_setupExplosion(100, 100, 0, 14, false, false);
	qa_check(explosion_opacity(0) == 8 && explosion_opacity(1) == NET_STYLE_SOLID,
	         "an explosion keeps the opacity set when it was spawned");
	memset(explosions, 0, sizeof(explosions));
	qa_check(rollback_state_hash() == hashBefore,
	         "...and that opacity never reaches the registered state byte stream");

	JE_setupExplosion(100, 100, 0, EXPLOSION_TYPES, false, false);
	JE_setupExplosion(100, 100, 0, -1, false, false);
	JE_setupExplosion(100, 100, 0, 97, false, false);
	bool spawned = false;
	for (unsigned int i = 0; i < COUNTOF(explosions); ++i)
		spawned = spawned || explosions[i].ttl != 0 || explosions[i].x != 0 || explosions[i].y != 0;
	qa_check(!spawned, "an explosion type outside the table spawns nothing");
	memset(explosions, 0, sizeof(explosions));

	// Partner shield bubbles follow hull opacity and Apply to Ship.
	{
		const bool savedNet = isNetworkGame;
		const JE_boolean savedTwo = twoPlayerMode;
		const uint savedPlayerNum = thisPlayerNum;
		const NetShipView savedView = netStyleView(0);
		const Player saved0 = player[0], saved1 = player[1];
		const JE_boolean savedEndless = endlessMode, savedMods = endlessCampaignMods;
		const int savedShieldFlash[2] = { shieldGaugeFlash[0], shieldGaugeFlash[1] };
		const int savedArmorFlash[2] = { armorGaugeFlash[0], armorGaugeFlash[1] };

		isNetworkGame = true;
		twoPlayerMode = true;
		endlessMode = false;          // avoid perk side effects
		endlessCampaignMods = false;
		thisPlayerNum = 1;             // local seat 0
		NetShipView view = savedView;
		view.opacity = 50;
		view.shipOpacity = true;
		netStyleSetView(0, view);

		for (uint seat = 0; seat < 2; ++seat)
		{
			player[seat].x = 100;
			player[seat].y = 100;
			player[seat].shield = 20;
			player[seat].shield_max = 20;
			player[seat].armor = 20;
			player[seat].is_alive = true;
		}

		memset(explosions, 0, sizeof(explosions));
		JE_playerDamage(1, &player[1]);
		qa_check(explosions[0].ttl != 0 && explosion_opacity(0) == 8,
		         "the partner's shield bubble fades with their hull");

		memset(explosions, 0, sizeof(explosions));
		JE_playerDamage(1, &player[0]);
		qa_check(explosions[0].ttl != 0 && explosion_opacity(0) == NET_STYLE_SOLID,
		         "...and this machine's own bubble stays solid");

		view.shipOpacity = false;   // Apply to Ship off
		netStyleSetView(0, view);
		memset(explosions, 0, sizeof(explosions));
		JE_playerDamage(1, &player[1]);
		qa_check(explosions[0].ttl != 0 && explosion_opacity(0) == NET_STYLE_SOLID,
		         "...and sparing their ship spares the bubble with it");

		player[1] = saved1;
		player[0] = saved0;
		shieldGaugeFlash[0] = savedShieldFlash[0]; shieldGaugeFlash[1] = savedShieldFlash[1];
		armorGaugeFlash[0] = savedArmorFlash[0];   armorGaugeFlash[1] = savedArmorFlash[1];
		endlessCampaignMods = savedMods;
		endlessMode = savedEndless;
		endlessSetFxPlayer(0);
		memset(explosions, 0, sizeof(explosions));
		netStyleSetView(0, savedView);
		thisPlayerNum = savedPlayerNum;
		twoPlayerMode = savedTwo;
		isNetworkGame = savedNet;
	}

	/* Both colour bytes sit in padding the structs already had. Widening either pool moves the
	 * registry layout and every replay fixture hash with it. */
	qa_check(sizeof(Explosion) == 14 && sizeof(rep_explosion_type) == 20,
	         "the explosion pools keep the widths the replay fixtures were recorded at");

	/* One opaque pixel is enough to verify the packed palette-bank and lift argument. */
	if (VGAScreen != NULL && VGAScreen->format->BitsPerPixel == 8)
	{
		union {
			Uint16 align;  // the offset table is read as Uint16, which needs the storage aligned
			Uint8 bytes[10];
		} frames = { .bytes = { 4, 0, 7, 0,      // two frames, one pixel each, both bank 7
		                        0x10, 0x78, 0x0f,   // shade 8
		                        0x10, 0x7f, 0x0f } };  // shade 15
		const Sprite2_array sheet = { sizeof(frames.bytes), frames.bytes };

		Uint8 *const row = (Uint8 *)VGAScreen->pixels;
		const Uint8 savedPixels[2] = { row[0], row[1] };
		const int blended = (8 + 2) / 2 + ENDLESS_EXPLOSION_BRIGHT;
		const int lifted = blended > 15 ? 15 : blended;

		row[0] = 0x02;  // shade 2, so the average is five before the lift
		blit_sprite2_blend_filter_clip(VGAScreen, 0, 0, sheet, 1,
		                              ENDLESS_ELITE_FILTER | ENDLESS_EXPLOSION_BRIGHT);
		row[1] = 0x0f;  // full shade over full shade: the lift can only overshoot the bank
		blit_sprite2_blend_filter_clip(VGAScreen, 1, 0, sheet, 2,
		                              ENDLESS_CHAMPION_FILTER | ENDLESS_EXPLOSION_BRIGHT);

		qa_check(row[0] == (ENDLESS_ELITE_FILTER | lifted)
		         && row[1] == (ENDLESS_CHAMPION_FILTER | 15),
		         "a tinted explosion pixel blends into its own bank, lifted and clamped there");

		row[0] = savedPixels[0];
		row[1] = savedPixels[1];
	}

	endlessMode = savedMode;
	endlessCampaignMods = savedCampaign;
}

/* An elite's bullets wear the tier bank its body does, stamped into the shot at spawn. The byte
 * carrying it is presentation, so it may not reach either hash a peer compares. */
void qa_test_elite_shot_tint(void)
{
	/* Like both explosion pools, the colour byte sits in padding the struct already had. Widening
	 * the pool moves the registry layout and every replay fixture hash with it. */
	qa_check(sizeof(EnemyShotType) == 32,
	         "the enemy-shot pool keeps the width the replay fixtures were recorded at");

	/* The pool hash walks live slots only, so freeing the rest leaves one bullet to compare. */
	JE_boolean savedAvail[ENEMY_SHOT_MAX];
	const EnemyShotType savedShot = enemyShot[0];
	memcpy(savedAvail, enemyShotAvail, sizeof(savedAvail));
	for (unsigned int i = 0; i < COUNTOF(enemyShotAvail); ++i)
		enemyShotAvail[i] = 1;

	enemyShotAvail[0] = 0;
	enemyShot[0].sx = 90;
	enemyShot[0].sy = 120;
	enemyShot[0].duration = 20;
	enemyShot[0].filter = 0;
	const Uint32 plain = network_sim_pools(NULL);
	enemyShot[0].filter = ENDLESS_CHAMPION_FILTER;
	qa_check(network_sim_pools(NULL) == plain,
	         "bullet colour leaves the pool hash a peer compares untouched");

	memcpy(enemyShotAvail, savedAvail, sizeof(savedAvail));
	enemyShot[0] = savedShot;

	/* The lift the bullets are drawn with, through the blitter replay draws them with. Same
	 * one-opaque-pixel frames the explosion tint check above builds. */
	if (VGAScreen != NULL && VGAScreen->format->BitsPerPixel == 8)
	{
		union {
			Uint16 align;  // the offset table is read as Uint16, which needs the storage aligned
			Uint8 bytes[10];
		} frames = { .bytes = { 4, 0, 7, 0,         // two frames, one pixel each, both bank 7
		                        0x10, 0x74, 0x0f,      // shade 4
		                        0x10, 0x7e, 0x0f } };  // shade 14
		const Sprite2_array sheet = { sizeof(frames.bytes), frames.bytes };

		Uint8 *const row = (Uint8 *)VGAScreen->pixels;
		const Uint8 savedPixels[2] = { row[0], row[1] };
		const int lifted = (4 + ENDLESS_SHOT_BRIGHT > 15) ? 15 : 4 + ENDLESS_SHOT_BRIGHT;

		blit_sprite2_filter_bright_clip(VGAScreen, 0, 0, sheet, 1,
		                                ENDLESS_ELITE_FILTER | ENDLESS_SHOT_BRIGHT);
		blit_sprite2_filter_bright_clip(VGAScreen, 1, 0, sheet, 2,
		                                ENDLESS_CHAMPION_FILTER | ENDLESS_SHOT_BRIGHT);

		qa_check(row[0] == (ENDLESS_ELITE_FILTER | lifted)
		         && row[1] == (ENDLESS_CHAMPION_FILTER | 15),
		         "a tinted bullet pixel keeps its own shade, lifted and clamped in its bank");

		row[0] = savedPixels[0];
		row[1] = savedPixels[1];
	}
}

/* The band JE_repaintTextWindow paints: the bar's left edge out to the bounty column. */
enum { QA_MSGBAR_X = 16, QA_MSGBAR_W = 244 - 16, QA_MSGBAR_H = 11 };

static void qa_copy_message_bar(Uint8 *out)
{
	const int y0 = vga_height - QA_MSGBAR_H;
	for (int y = 0; y < QA_MSGBAR_H; ++y)
		memcpy(&out[y * QA_MSGBAR_W],
		       (Uint8 *)VGAScreenSeg->pixels + (y0 + y) * VGAScreenSeg->pitch + QA_MSGBAR_X,
		       QA_MSGBAR_W);
}

static bool qa_message_bar_differs(const Uint8 *was)
{
	Uint8 now[QA_MSGBAR_H * QA_MSGBAR_W];
	qa_copy_message_bar(now);
	return memcmp(now, was, sizeof(now)) != 0;
}

/* The tier name uses its own palette bank without shifting the text that follows it. */
void qa_test_elite_message_tint(void)
{
	if (VGAScreenSeg == NULL || VGAScreenSeg->format->BitsPerPixel != 8)
		return;  // the draws below paint the real message bar

	const bool savedSilent = rollback_resim_silent, savedDirty = hud_message_dirty;
	const JE_word savedErase = textErase;
	rollback_resim_silent = false;  // a silent re-simulation pass blits nothing

	Uint8 plain[QA_MSGBAR_H * QA_MSGBAR_W], tinted[QA_MSGBAR_H * QA_MSGBAR_W];

	JE_drawTextWindow("Elite Enemy destroyed!");
	qa_copy_message_bar(plain);

	JE_drawTextWindowSplit("Elite Enemy", ENDLESS_ELITE_FILTER >> 4, " destroyed!", "", 244);
	qa_copy_message_bar(tinted);

	int moved = 0, recoloured = 0;
	for (int i = 0; i < QA_MSGBAR_H * QA_MSGBAR_W; ++i)
	{
		if (tinted[i] == plain[i])
			continue;
		if ((plain[i] & 0xf0) == 0 && tinted[i] == (ENDLESS_ELITE_FILTER | plain[i]))
			++recoloured;
		else
			++moved;
	}

	qa_check(moved == 0, "colouring the tier name leaves the rest of the bounty line where it was");
	qa_check(recoloured > 0, "the tier name is painted in the elite bank at the shades it had");

	/* Online a kill can land in a rollback pass that draws nothing, so the line is held and
	 * repainted on the next pass that reaches the screen. It has to come back with its tint. */
	JE_drawTextWindow("");  // blank the bar first, so anything the silent post paints shows up
	rollback_resim_silent = true;
	JE_drawTextWindowSplit("Elite Enemy", ENDLESS_ELITE_FILTER >> 4, " destroyed!", "", 244);
	rollback_resim_silent = false;

	qa_check(hud_message_dirty && qa_message_bar_differs(tinted),
	         "a bounty line posted in a silent pass paints nothing and is held for a visible one");

	JE_repaintTextWindow();
	qa_check(!qa_message_bar_differs(tinted),
	         "the held bounty line comes back with its tier colour on the next visible pass");

	textErase = savedErase;
	hud_message_dirty = savedDirty;
	rollback_resim_silent = savedSilent;
}

/* Every kill-fire drive, driven through the real kill path: the window has to open, the boon has
 * to grant what it advertises and nothing it does not, and a curse has to bite. */
void qa_test_kill_fire_drives(void)
{
	const JE_boolean savedEndless = endlessMode, savedCoop = coopEndlessMode;
	const Uint64 savedActive = endlessActiveMods;
	const int savedKills = endlessRunKills;

	endlessMode = true;
	coopEndlessMode = true;
	memset(endlessPerkOwned, 0, sizeof(endlessPerkOwned));
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));

	struct { Uint64 bit; const char *name; bool fire; bool damage; bool jam; const char *evil; } drives[] = {
		{ ENDLESS_MOD_TURBODRIVE, "Turbodrive", true,  false, false, "" },
		{ ENDLESS_MOD_OVERBLAST,  "Overblast",  false, true,  false, "" },
		{ ENDLESS_MOD_OVERDRIVE,  "Overdrive",  true,  true,  false, "" },
		{ ENDLESS_MOD_BACKFIRE,   "Backfire",   false, false, true,  "JAMMED" },
		{ ENDLESS_MOD_BURNOUT,    "Burnout",    false, false, true,  "BURNOUT" },
		{ ENDLESS_MOD_MISFIRE,    "Misfire",    false, false, false, "MISFIRE" },
	};

	for (unsigned d = 0; d < COUNTOF(drives); ++d)
	{
		// Player 0 buys the drive; player 1 buys nothing and must come away with nothing.
		endlessActiveMods = 0;
		endlessPurchasedMods[0] = (unsigned)drives[d].bit;
		endlessPurchasedMods[1] = 0;
		endlessBuffCharge[0] = 10;
		endlessBuffCharge[1] = 0;
		endlessApplyPurchasedMods();
		memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
		memset(endlessComboKills, 0, sizeof(endlessComboKills));
		memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));

		// Enough kills to climb a step of the combo ramp and stack the damage terms.
		for (int k = 0; k < 60; ++k)
			endlessCountKill(0, ENDLESS_KILLER_NONE);

		endlessSetFxPlayer(0);
		char detail[96];
		snprintf(detail, sizeof(detail), "%s opens its kill-fire window", drives[d].name);
		qa_check(endlessTurbodriveActive() && endlessComboKills[0] == 60, detail);

		snprintf(detail, sizeof(detail), "%s window length follows the charge its buyer paid",
		         drives[d].name);
		qa_check(endlessTurbodriveTimer[0] == endlessBuffWindowTicksFor(0)
		         && endlessBuffWindowTicksFor(0) > endlessBuffWindowTicksFor(1), detail);

		snprintf(detail, sizeof(detail), "%s %s the guns", drives[d].name,
		         drives[d].fire ? "quickens" : "leaves the cadence alone");
		qa_check((endlessKillBuffFireDecrements() > 0) == drives[d].fire, detail);

		snprintf(detail, sizeof(detail), "%s %s shot damage", drives[d].name,
		         drives[d].damage ? "stacks" : "does not stack");
		qa_check((endlessKillBuffDamagePercent() > 0) == drives[d].damage, detail);

		snprintf(detail, sizeof(detail), "%s %s", drives[d].name,
		         drives[d].jam ? "jams the guns" : "does not jam the guns");
		qa_check((endlessKillFireJamTicks() > 0) == drives[d].jam, detail);

		const bool evil = drives[d].evil[0] != '\0';
		snprintf(detail, sizeof(detail), "%s reads as %s", drives[d].name, evil ? "a curse" : "a boon");
		qa_check(endlessKillFireIsEvil() == evil
		         && strcmp(endlessKillFireEvilName(), drives[d].evil) == 0, detail);

		snprintf(detail, sizeof(detail), "%s moves the buyer's shot damage the right way",
		         drives[d].name);
		const int dmg = endlessPlayerDamagePercent();
		qa_check(drives[d].damage ? (dmg > 100) : (evil ? (dmg <= 100) : (dmg == 100)), detail);

		// The partner bought nothing, so none of it reaches them.
		endlessSetFxPlayer(1);
		snprintf(detail, sizeof(detail), "%s leaves the other ship untouched", drives[d].name);
		qa_check(!endlessTurbodriveActive() && endlessKillBuffFireDecrements() == 0
		         && endlessKillBuffDamagePercent() == 0 && endlessKillFireJamTicks() == 0
		         && endlessPlayerDamagePercent() == 100 && endlessShipTintFilter() == 0, detail);

		endlessSetFxPlayer(0);
		snprintf(detail, sizeof(detail), "%s tints the buyer's hull", drives[d].name);
		qa_check(endlessShipTintFilter() != 0, detail);
	}

	/* Each ship's Opening Salvo window comes from its own drive. */
	endlessActiveMods = 0;
	endlessPurchasedMods[0] = ENDLESS_MOD_TURBODRIVE;
	endlessPurchasedMods[1] = ENDLESS_MOD_OVERBLAST;
	endlessBuffCharge[0] = 0;
	endlessBuffCharge[1] = 20;
	endlessApplyPurchasedMods();
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
	endlessSetFxPlayer(0);   // the context stays on player 0 across the whole burst
	for (int k = 0; k < 30; ++k)
		endlessCountKill(0, ENDLESS_KILLER_NONE);

	qa_check(endlessTurbodriveTimer[0] == endlessBuffWindowTicksFor(0)
	         && endlessTurbodriveTimer[1] == endlessBuffWindowTicksFor(1)
	         && endlessTurbodriveTimer[1] > endlessTurbodriveTimer[0],
	         "each ship's window is as long as its own drive was paid for");

	endlessSetFxPlayer(0);
	const bool p0 = endlessKillBuffFireDecrements() > 0 && endlessKillBuffDamagePercent() == 0;
	endlessSetFxPlayer(1);
	const bool p1 = endlessKillBuffFireDecrements() == 0 && endlessKillBuffDamagePercent() > 0;
	qa_check(p0 && p1, "two ships fly different drives at once, each getting only its own");

	endlessSetFxPlayer(0);
	endlessActiveMods = savedActive;
	endlessRunKills = savedKills;
	memset(endlessPlayerMods, 0, sizeof(endlessPlayerMods));
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
	endlessPurchasedMods[0] = endlessPurchasedMods[1] = 0;
	endlessBuffCharge[0] = endlessBuffCharge[1] = 0;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
}

/* The movement tick must apply each ship's drive to that ship's weapon cooldowns. */
void qa_test_kill_fire_wiring(void)
{
	const JE_boolean savedEndless = endlessMode, savedCoop = coopEndlessMode;
	const Uint64 savedActive = endlessActiveMods;
	const int savedKills = endlessRunKills;

	endlessMode = true;
	coopEndlessMode = true;
	memset(endlessPerkOwned, 0, sizeof(endlessPerkOwned));
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));

	// Both ships buy Turbodrive; both should have their guns quickened.
	endlessActiveMods = 0;
	endlessPurchasedMods[0] = ENDLESS_MOD_TURBODRIVE;
	endlessPurchasedMods[1] = ENDLESS_MOD_TURBODRIVE;
	endlessBuffCharge[0] = endlessBuffCharge[1] = 0;
	endlessApplyPurchasedMods();
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
	for (int k = 0; k < 30; ++k)
		endlessCountKill(0, ENDLESS_KILLER_NONE);

	int dropped[2] = { 0, 0 };
	for (uint p = 0; p < 2; ++p)
	{
		endlessSetFxPlayer(p);              // JE_playerMovement sets this from this_player
		shotRepeat[SHOT_FRONT] = 100;
		endlessPerShipTick(&player[p]);     // the real per-tick block, gate included
		dropped[p] = 100 - shotRepeat[SHOT_FRONT];
	}
	qa_check(dropped[0] > 0 && dropped[1] > 0,
	         "a drive quickens the guns of every ship that bought one, not just player 1");

	// Only player 2 buys one: player 1's guns must stay at their stock cadence.
	endlessActiveMods = 0;
	endlessPurchasedMods[0] = 0;
	endlessPurchasedMods[1] = ENDLESS_MOD_TURBODRIVE;
	endlessApplyPurchasedMods();
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	for (int k = 0; k < 30; ++k)
		endlessCountKill(0, ENDLESS_KILLER_NONE);

	for (uint p = 0; p < 2; ++p)
	{
		endlessSetFxPlayer(p);
		shotRepeat[SHOT_FRONT] = 100;
		endlessPerShipTick(&player[p]);
		dropped[p] = 100 - shotRepeat[SHOT_FRONT];
	}
	qa_check(dropped[0] == 0 && dropped[1] > 0,
	         "a drive the second ship bought quickens the second ship alone");

	/* Opening Salvo charge belongs to the ship that earned and spends it. */
	endlessPerkTakenBy[0][PERK_SALVO] = 1;
	endlessPerkTakenBy[1][PERK_SALVO] = 1;
	endlessPerkRederive();
	endlessResetZonePerkTimers();      // both ships start a zone charged
	endlessSetFxPlayer(0);
	qa_check(endlessOpeningSalvoConsume() && endlessOpeningSalvoVolleyActive(),
	         "the first ship spends its own Opening Salvo");
	endlessSetFxPlayer(1);
	qa_check(!endlessOpeningSalvoVolleyActive(),
	         "...and the second ship's salvo is still banked");
	qa_check(endlessOpeningSalvoConsume() && endlessOpeningSalvoVolleyActive(),
	         "...for the second ship to spend itself");

	/* The Salvo gauge reports the local ship's banked charge and active window. */
	endlessResetZonePerkTimers();
	endlessSetFxPlayer(0);
	qa_check(endlessOpeningSalvoGaugePercent() == 100, "a banked Opening Salvo fills its owner's gauge");
	endlessOpeningSalvoConsume();
	int salvoPct = endlessOpeningSalvoGaugePercent();
	bool salvoRecedes = (salvoPct > 0);
	for (int t = 0; t < ENDLESS_PERK_SALVO_WINDOW; ++t)
	{
		endlessOpeningSalvoTick();          // the run-wide walk, exactly as the sim tick calls it
		const int now = endlessOpeningSalvoGaugePercent();
		if (now > salvoPct)
			salvoRecedes = false;
		salvoPct = now;
	}
	qa_check(salvoRecedes && salvoPct == 0, "firing drains the gauge as the salvo window runs out");
	endlessSetFxPlayer(1);
	qa_check(endlessOpeningSalvoGaugePercent() == 100,
	         "...and the ship that held its fire still reads a full charge");

	endlessPerkTakenBy[1][PERK_SALVO] = 0;
	endlessPerkRederive();
	qa_check(endlessOpeningSalvoGaugePercent() == 0,
	         "a ship without the perk has no green on its gauge at all");
	endlessResetZonePerkTimers();
	qa_check(!endlessOpeningSalvoConsume(),
	         "a ship that never picked Opening Salvo has none to spend");

	/* The Salvo window must open before a same-tick special fires. */
	endlessPerkTakenBy[0][PERK_SALVO] = 1;
	endlessPerkTakenBy[1][PERK_SALVO] = 1;
	endlessPerkRederive();
	endlessResetZonePerkTimers();

	const JE_boolean savedTwo = twoPlayerMode, savedLinked = twoPlayerLinked;
	const bool savedFire = button[0];
	const JE_byte savedRepeat = shotRepeat[SHOT_FRONT];
	const Uint8 savedGun[2] = { player[0].items.weapon[SHOT_FRONT].id,
	                            player[1].items.weapon[SHOT_FRONT].id };

	twoPlayerMode = true;
	twoPlayerLinked = false;
	player[0].items.weapon[SHOT_FRONT].id = player[1].items.weapon[SHOT_FRONT].id = 1;
	shotRepeat[SHOT_FRONT] = 0;
	button[0] = true;

	endlessSetFxPlayer(0);
	qa_check(endlessArmOpeningSalvoForTick(&player[0], 1) && endlessOpeningSalvoVolleyActive(),
	         "the volley's window is open before the tick's special goes out");
	endlessSetFxPlayer(1);
	qa_check(!endlessOpeningSalvoVolleyActive(),
	         "...and open for the ship that pulled the trigger alone");
	qa_check(endlessArmOpeningSalvoForTick(&player[1], 2) && endlessOpeningSalvoVolleyActive(),
	         "...leaving the second ship its own to open from its own bank");

	// Nothing fires on a recharging gun or a released trigger, so neither may spend the charge.
	endlessResetZonePerkTimers();
	endlessSetFxPlayer(0);
	shotRepeat[SHOT_FRONT] = 5;
	qa_check(!endlessArmOpeningSalvoForTick(&player[0], 1) && !endlessOpeningSalvoVolleyActive()
	         && endlessOpeningSalvoGaugePercent() == 100,
	         "a gun still recharging opens no window and keeps the charge banked");
	shotRepeat[SHOT_FRONT] = 0;
	button[0] = false;
	qa_check(!endlessArmOpeningSalvoForTick(&player[0], 1) && endlessOpeningSalvoGaugePercent() == 100,
	         "...and so does a released trigger");
	button[0] = true;
	player[0].items.weapon[SHOT_FRONT].id = 0;
	qa_check(!endlessArmOpeningSalvoForTick(&player[0], 1) && endlessOpeningSalvoGaugePercent() == 100,
	         "...and an empty front bay, which has no volley to open one for");

	twoPlayerMode = savedTwo;
	twoPlayerLinked = savedLinked;
	button[0] = savedFire;
	shotRepeat[SHOT_FRONT] = savedRepeat;
	player[0].items.weapon[SHOT_FRONT].id = savedGun[0];
	player[1].items.weapon[SHOT_FRONT].id = savedGun[1];

	endlessSetFxPlayer(0);
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessResetZonePerkTimers();
	endlessActiveMods = savedActive;
	endlessRunKills = savedKills;
	memset(endlessPlayerMods, 0, sizeof(endlessPlayerMods));
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
	endlessPurchasedMods[0] = endlessPurchasedMods[1] = 0;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
}

/* Kinetic Converter's recharge, magazine and charge-ramp payouts each scale with the stacks, stay
 * bounded, and stay personal: in co-op only the ship that took the hit may be paid for it. */
void qa_test_kinetic_converter(void)
{
	const JE_boolean savedEndless = endlessMode, savedCoop = coopEndlessMode;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	endlessMode = true;
	coopEndlessMode = true;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	memset(endlessPerkKineticAmmoAccum, 0, sizeof(endlessPerkKineticAmmoAccum));
	endlessPerkRederive();
	endlessSetFxPlayer(0);

	const int maxStack = endlessPerkMaxStack(PERK_KINETIC);

	qa_check(endlessPerkKineticCooldownCut(200) == 0 && endlessPerkKineticAmmoRounds() == 0
	         && endlessPerkKineticChargeStages() == 0,
	         "a ship without Kinetic Converter is paid nothing for taking a hit");

	// The cut must be monotonic in the stacks, bounded by the time left, and at least a tick.
	bool cutGrows = true, cutBounded = true;
	int prevCut = 0;
	for (int s = 1; s <= maxStack; ++s)
	{
		endlessPerkTakenBy[0][PERK_KINETIC] = (JE_byte)s;
		endlessPerkRederive();
		const int cut = endlessPerkKineticCooldownCut(200);
		cutGrows &= cut > prevCut;
		prevCut = cut;
		cutBounded &= cut <= 200 && endlessPerkKineticCooldownCut(1) == 1
		           && endlessPerkKineticCooldownCut(0) == 0;
	}
	qa_check(cutGrows, "each Kinetic Converter stack takes more off the special recharge");
	qa_check(cutBounded, "...but never more of the clock than is left on it");

	// Four hits is one whole round per stack at the 25%-per-stack rate, carry included.
	for (int s = 1; s <= maxStack; ++s)
	{
		endlessPerkTakenBy[0][PERK_KINETIC] = (JE_byte)s;
		endlessPerkRederive();
		memset(endlessPerkKineticAmmoAccum, 0, sizeof(endlessPerkKineticAmmoAccum));
		int rounds = 0;
		for (int hit = 0; hit < 4; ++hit)
			rounds += endlessPerkKineticAmmoRounds();
		qa_check(rounds == s, "four absorbed hits give back one sidekick round per Kinetic stack");
		qa_check(endlessPerkKineticChargeStages() == s,
		         "...and every hit walks a charge sidekick one stage per stack");
	}

	// Twiddle charges: cheaper with every stack, never free, and never dearer than the list price.
	endlessSetFxPlayer(0);
	endlessPerkTakenBy[0][PERK_KINETIC] = 0;
	endlessPerkRederive();
	qa_check(endlessPerkKineticTwiddleCost(20) == 20 && endlessPerkKineticTwiddleCost(0) == 0,
	         "a ship without Kinetic Converter pays a twiddle's list price");

	bool costFalls = true, costBounded = true;
	int prevCost = endlessPerkKineticTwiddleCost(20);
	for (int s = 1; s <= maxStack; ++s)
	{
		endlessPerkTakenBy[0][PERK_KINETIC] = (JE_byte)s;
		endlessPerkRederive();
		const int paid = endlessPerkKineticTwiddleCost(20);
		costFalls &= paid < prevCost;
		prevCost = paid;
		costBounded &= paid > 0 && endlessPerkKineticTwiddleCost(1) == 1;
	}
	qa_check(costFalls, "each Kinetic Converter stack takes more off a twiddle's charge");
	qa_check(costBounded && prevCost == 7, "...down to a third of a 20-point charge, never free");

	// Only the first ship picks it, so the partner's hits pay nothing and each carry stays its own.
	memset(endlessPerkKineticAmmoAccum, 0, sizeof(endlessPerkKineticAmmoAccum));
	endlessPerkTakenBy[0][PERK_KINETIC] = (JE_byte)maxStack;
	endlessPerkTakenBy[1][PERK_KINETIC] = 0;
	endlessPerkRederive();
	int paid[2] = { 0, 0 };
	for (int hit = 0; hit < 4; ++hit)
		for (uint p = 0; p < 2; ++p)
		{
			endlessSetFxPlayer(p);
			paid[p] += endlessPerkKineticAmmoRounds();
		}
	endlessSetFxPlayer(1);
	qa_check(paid[0] == maxStack && paid[1] == 0 && endlessPerkKineticCooldownCut(200) == 0
	         && endlessPerkKineticChargeStages() == 0,
	         "only the ship that bought Kinetic Converter is paid for a hit");

	endlessSetFxPlayer(0);
	memset(endlessPerkKineticAmmoAccum, 0, sizeof(endlessPerkKineticAmmoAccum));
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
}

static int qa_live_player_shots(void)
{
	int live = 0;
	for (int i = 0; i < MAX_PWEAPON; ++i)
		live += shotAvail[i] != 0;
	return live;
}

void qa_test_zica_buff_fire(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const bool savedBuff = reworkEnabled[REWORK_ZICA_BUFF];
	const bool savedInfinite = cheatInfiniteGenerator;
	const uint savedPower = power;
	const JE_word lv10 = weaponPort[ZICA_LASER_PORT].op[0][ZICA_LV10_OP];
	const JE_word lv11 = weaponPort[ZICA_LASER_PORT].op[0][ZICA_LV11_OP];

	endlessCampaignMods = false;
	cheatInfiniteGenerator = false;

	int shots[3];
	uint spent[3];
	for (int run = 0; run < 3; ++run)
	{
		endlessMode = run == 2;
		reworkEnabled[REWORK_ZICA_BUFF] = run == 1;
		memset(shotAvail, 0, sizeof(shotAvail));
		shotMultiPos[SHOT_FRONT] = 0;
		power = 10000;
		player_shot_fire_port(ZICA_LASER_PORT, SHOT_FRONT, 100, 150, 0, 0, 0, ZICA_LV11_OP, 1);
		spent[run] = 10000 - power;
		shots[run] = qa_live_player_shots();
	}
	qa_check(shots[0] == weapons[lv11].multi && shots[1] == shots[0] + weapons[lv10].multi,
	         "the Zica Laser Buff rework adds the Lv10 beam to a Lv11 volley");
	qa_check(spent[0] > 0 && spent[1] == spent[0], "...at no extra generator cost");
	qa_check(shots[2] == shots[1], "Endless always fires the buffed volley");

	endlessMode = false;
	reworkEnabled[REWORK_ZICA_BUFF] = true;
	memset(shotAvail, 0, sizeof(shotAvail));
	shotMultiPos[SHOT_FRONT] = 0;
	power = 10000;
	player_shot_fire_port(ZICA_LASER_PORT, SHOT_FRONT, 100, 150, 0, 0, 0, ZICA_LV10_OP, 1);
	qa_check(qa_live_player_shots() == weapons[lv10].multi, "...and leaves the other levels alone");

	memset(shotAvail, 0, sizeof(shotAvail));
	power = savedPower;
	cheatInfiniteGenerator = savedInfinite;
	reworkEnabled[REWORK_ZICA_BUFF] = savedBuff;
	endlessCampaignMods = savedCampaign;
	endlessMode = savedEndless;
}

void qa_test_orange_shield_rework(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const bool savedRing = reworkEnabled[REWORK_ORANGE_SHIELD];
	const JE_word ringGun = 749;
	qa_check(weapons[ringGun].circlesize != 0 && weapons[ringGun].sx[0] == SHOT_ATTACHED_VEL_REST
	         && weapons[ringGun].sy[0] == SHOT_ATTACHED_VEL_REST,
	         "the Orange Shield test's ring gun is the one it assumes");

	endlessCampaignMods = false;
	int x[3], y[3];
	for (int run = 0; run < 3; ++run)
	{
		endlessMode = run == 0;
		reworkEnabled[REWORK_ORANGE_SHIELD] = run == 1;
		memset(shotAvail, 0, sizeof(shotAvail));
		shotMultiPos[SHOT_SPECIAL2] = 0;
		const JE_integer id = player_shot_create(0, SHOT_SPECIAL2, 100, 150, 0, 0, ringGun, 1);
		x[run] = id < MAX_PWEAPON ? playerShotData[0].shotX : -1;
		y[run] = id < MAX_PWEAPON ? playerShotData[0].shotY : -1;
	}
	qa_check(x[0] != -1 && x[1] == x[0] && y[1] == y[0],
	         "the Orange Shield rework centres the ring on the ship, as Endless does");
	qa_check(x[2] != x[0] || y[2] != y[0], "...and without it the ring keeps its shipped offset");

	memset(shotAvail, 0, sizeof(shotAvail));
	reworkEnabled[REWORK_ORANGE_SHIELD] = savedRing;
	endlessCampaignMods = savedCampaign;
	endlessMode = savedEndless;
}

void qa_test_ramming_rework(void)
{
	static const struct
	{
		bool rework;
		uint invulnerable;
		bool rams, pays;
		const char *what;
	} rows[] = {
		{ false, 0,  true,  false, "without the rework a ram removes the enemy and pays nothing" },
		{ true,  0,  true,  true,  "the Ramming rework pays a ram kill and counts it" },
		{ false, 20, false, false, "an invulnerable ship does not ram without the rework" },
		{ true,  20, true,  true,  "...but rams on the rework's cadence" },
		{ true,  19, false, false, "...and only on that cadence" },
	};

	static JE_MultiEnemyType savedEnemies;
	JE_EnemyAvailType savedAvail;
	memcpy(savedEnemies, enemy, sizeof(enemy));
	memcpy(savedAvail, enemyAvail, sizeof(enemyAvail));
	const Player savedShip = player[0];
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const bool savedRamming = reworkEnabled[REWORK_RAMMING];
	const JE_byte savedRate = damageRate, savedNoclip = noclipMode;
	const JE_word savedKilled = enemyKilled;

	endlessMode = false;
	endlessCampaignMods = false;
	damageRate = 10;
	noclipMode = 0;
	for (size_t r = 0; r < COUNTOF(rows); ++r)
	{
		memset(enemyAvail, 1, sizeof(enemyAvail));
		memset(&enemy[0], 0, sizeof(enemy[0]));
		enemyAvail[0] = 0;
		enemy[0].ex = 100;
		enemy[0].ey = 150;
		enemy[0].armorleft = 1;
		enemy[0].enemytype = 1;
		enemy[0].evalue = 300;
		player[0].x = 100;
		player[0].y = 150;
		player[0].is_alive = true;
		player[0].armor = 28;
		player[0].invulnerable_ticks = rows[r].invulnerable;
		reworkEnabled[REWORK_RAMMING] = rows[r].rework;

		const Sint64 cash = player[0].cash;
		const JE_word killed = enemyKilled;
		JE_playerCollide(&player[0], 1);
		const bool rammed = enemyAvail[0] == 1;
		const bool paid = player[0].cash == cash + 300 && enemyKilled == killed + 1;
		qa_check(rammed == rows[r].rams && paid == rows[r].pays, rows[r].what);
	}
	JE_resetSP();
	memset(explosions, 0, sizeof(explosions));

	enemyKilled = savedKilled;
	noclipMode = savedNoclip;
	damageRate = savedRate;
	reworkEnabled[REWORK_RAMMING] = savedRamming;
	endlessCampaignMods = savedCampaign;
	endlessMode = savedEndless;
	player[0] = savedShip;
	memcpy(enemyAvail, savedAvail, sizeof(enemyAvail));
	memcpy(enemy, savedEnemies, sizeof(enemy));
}

static JE_integer qa_exit_shot(JE_word gr, int y, int ym, int xm)
{
	const JE_integer id = player_shot_create(0, SHOT_SPECIAL, 100, 100, 0, 0, 29, 1);
	if (id < MAX_PWEAPON)
	{
		PlayerShotDataType *const s = &playerShotData[id];
		s->shotTrail = 255;
		s->shotGr = gr;
		s->shotAni = 0;
		s->shotAniMax = 1;
		s->shotX = 100;
		s->shotY = y;
		s->shotYM = ym;
		s->shotXM = xm;
	}
	return id;
}

void qa_test_shot_exit_margin(void)
{
	const uint savedPower = power;
	const JE_boolean savedBackground2 = background2;
	const Sprite2_array savedSheet = spriteSheet8;
	power = 10000;
	memset(shotAvail, 0, sizeof(shotAvail));
	shotMultiPos[SHOT_SPECIAL] = 0;

	const JE_word ballGr = weapons[720].sg[0];
	qa_check(weapons[29].sy[0] == 1 && weapons[29].sg[0] == 271
	         && ballGr > 60000 && ballGr - 60001u < sprite_table[OPTION_SHAPES].count,
	         "the exit-margin test's MineField mine and Xega Ball are the ones it assumes");

	static const struct
	{
		bool cell;
		JE_word gr;
		int y, ym, xm;
		bool stays;
	} exits[] = {
		{ false, 271, -39, -1, 0, true },
		{ false, 271, -40, -1, 0, false },
		{ false, 271, -16, 0, 0, true },
		{ false, 271, -22, 0, 0, true },
		{ false, 271, -23, 0, 0, false },
		{ false, 271, -16, 0, SHOT_ATTACHED_VEL_REST, false },
		{ false, 1000 + 271, -23, 0, 0, false },
		{ true, 1, -16, 0, 0, false },
		{ true, 2, -23, 0, 0, true },
		{ true, 2, -24, 0, 0, false },
	};
	QaCellFrame cell;
	const Sprite2_array cellSheet = qa_cell_sheet(&cell);
	bool exitsRight = true;
	for (size_t i = 0; i < COUNTOF(exits); ++i)
		for (int shadows = 0; shadows < 2; ++shadows)
		{
			spriteSheet8 = exits[i].cell ? cellSheet : savedSheet;
			background2 = shadows != 0;
			const JE_integer id = qa_exit_shot(exits[i].gr, exits[i].y, exits[i].ym, exits[i].xm);
			if (id >= MAX_PWEAPON || (qa_shot_step(id) && shotAvail[id] != 0) != exits[i].stays)
				exitsRight = false;
			if (id < MAX_PWEAPON)
				shotAvail[id] = 0;
		}
	spriteSheet8 = savedSheet;
	qa_check(exitsRight,
	         "a shot past the top edge stays exactly while its sprite or shadow can show, "
	         "whether or not this machine draws shadows");

	background2 = true;
	const int ballH = sprite(OPTION_SHAPES, ballGr - 60001u)->height;
	const JE_integer tall = qa_exit_shot(ballGr, 2 - ballH, -1, 0);
	const bool tallStays = tall < MAX_PWEAPON && qa_shot_step(tall) && shotAvail[tall] != 0;
	const bool tallGoes = tall < MAX_PWEAPON && !qa_shot_step(tall) && shotAvail[tall] == 0;
	qa_check(ballH > 41 && tallStays && tallGoes,
	         "a special taller than the deep margin stays while its last row shows, then goes");

	memset(shotAvail, 0, sizeof(shotAvail));
	background2 = savedBackground2;
	power = savedPower;
}

void qa_test_shot_shadow_edge(void)
{
	const uint savedPower = power;
	const JE_boolean savedBackground2 = background2;
	power = 10000;
	memset(shotAvail, 0, sizeof(shotAvail));
	shotMultiPos[SHOT_SPECIAL] = 0;

	const size_t bytes = (size_t)VGAScreen->pitch * (size_t)VGAScreen->h;
	Uint8 *const savedScreen = malloc(bytes);
	Uint8 *const plain = malloc(bytes);
	const JE_integer id = player_shot_create(0, SHOT_SPECIAL, 100, 100, 0, 0, 29, 1);
	qa_check(savedScreen != NULL && plain != NULL && id < MAX_PWEAPON,
	         "the shadow-edge test's buffers and MineField mine are ready");
	if (savedScreen != NULL && plain != NULL && id < MAX_PWEAPON)
	{
		memcpy(savedScreen, VGAScreen->pixels, bytes);
		PlayerShotDataType *const s = &playerShotData[id];
		s->shotTrail = 255;
		s->shotY = 0;
		bool tracksShadow = true, sawEdge = false;
		for (int tick = 0; tick < 60 && shotAvail[id] != 0; ++tick)
		{
			const PlayerShotDataType before = *s;
			const JE_byte avail = shotAvail[id];
			const JE_word frame = s->shotGr + s->shotAni;

			memset(VGAScreen->pixels, 0x57, bytes);
			background2 = false;
			qa_shot_step(id);
			memcpy(plain, VGAScreen->pixels, bytes);

			*s = before;
			shotAvail[id] = avail;
			memset(VGAScreen->pixels, 0x57, bytes);
			background2 = true;
			qa_shot_step(id);
			if (shotAvail[id] == 0)
				break;

			const int shadowY = s->shotY + shadowYDist;
			const bool drawn = memcmp(plain, VGAScreen->pixels, bytes) != 0;
			const bool showing = sprite2_has_pixel_in_window(0, shadowY, spriteSheet8, frame,
			                                                 INT_MIN, INT_MAX, 0, INT_MAX);
			if (drawn != showing)
				tracksShadow = false;
			if (showing && shadowY < 0)
				sawEdge = true;
		}
		qa_check(tracksShadow && sawEdge,
		         "a rising shot's shadow is drawn exactly while any row of it is on screen");
		memcpy(VGAScreen->pixels, savedScreen, bytes);
	}

	free(savedScreen);
	free(plain);
	memset(shotAvail, 0, sizeof(shotAvail));
	background2 = savedBackground2;
	power = savedPower;
}
