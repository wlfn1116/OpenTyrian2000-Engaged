#define ENDLESS_INTERNAL_ACCESS
#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "config.h"
#include "endless.h"
#include "enemy_death.h"
#include "episodes.h"
#include "endless_internal.h"
#include "explosions.h"
#include "game_loop.h"
#include "game_state.h"
#include "hud.h"
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

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where a projectile is collided from. The geometry comes from sprite data alone, so a synthetic
 * frame pins it exactly; the shot loops in game_loop.c add the target's own middle to the
 * answer. */
void qa_test_shot_hitboxes(void)
{
	/* A 12-pixel test cell with two four-pixel rows exercises the packed sprite controls. */
	static const Uint8 body[] = {
		0x0c,                                // row 0: 12 transparent columns, then the next row
		0x42, 0x11, 0x11, 0x11, 0x11, 0x06,  // row 1: skip 2, four pixels, skip the rest
		0x42, 0x11, 0x11, 0x11, 0x11, 0x06,  // row 2: the same
		0x0f,
	};
	union {
		Uint16 align;  // the offset table is read as Uint16, which needs the storage aligned
		Uint8 bytes[2 + sizeof(body)];
	} frame;
	frame.bytes[0] = 2;  // one-entry offset table, little-endian, pointing just past itself
	frame.bytes[1] = 0;
	memcpy(frame.bytes + 2, body, sizeof(body));

	const Sprite2_array sheet = { sizeof(frame.bytes), frame.bytes };

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
	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
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
	bool outSpecial;
	int outX, outY, outHitDx, outHitDy;
	JE_integer outDmg;
	JE_byte outBlast, outChain, outOwner;
	JE_word outRadiusW, outRadiusH;
	s->shotTrail = 255;
	s->shotX = 100;
	s->shotY = -20;
	qa_check(player_shot_move_and_draw(id, &outSpecial, &outX, &outY, &outDmg, &outBlast,
	                                   &outChain, &outOwner, &outRadiusW, &outRadiusH,
	                                   &outHitDx, &outHitDy)
	         && shotAvail[id] != 0,
	         "above the screen a re-encoded beam still ascends and survives to the deep margin");
	s->shotY = -20;
	s->shotYM = SHOT_ATTACHED_VEL_REST;
	qa_check(!player_shot_move_and_draw(id, &outSpecial, &outX, &outY, &outDmg, &outBlast,
	                                    &outChain, &outOwner, &outRadiusW, &outRadiusH,
	                                    &outHitDx, &outHitDy)
	         && shotAvail[id] == 0,
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

	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	power = savedPower;
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
	guidedShotScreenAim = savedGuidedAim;
}

// Survey drop fixtures include JE_setupEnemy's scatter RNG draw for each pickup.
static int qa_surveyor_count_loot(int *bombs, int *orbs, int *cubes, int *misplaced,
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
		else
			++*misplaced;
	}
	return loot;
}

/* Surveyor rolls each linked hull once and places all drops at that hull's kill anchor. */
void qa_test_surveyor_drop_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	const Uint64 savedActive = endlessActiveMods;
	Uint64 savedPlayerMods[2];
	memcpy(savedPlayerMods, endlessPlayerMods, sizeof(savedPlayerMods));
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	endlessActiveMods = 0;
	memset(endlessPlayerMods, 0, sizeof(endlessPlayerMods));
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);

	qa_check(enemyDat[800].value == -4 && enemyDat[535].value == -3,
	         "the survey drop test's pickups are the two the collector special-cases");
	qa_check(enemyDat[513].value == 1,
	         "the survey drop test's cube is the datacube the special grant answers");

	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}

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
	int bombs = 0, orbs = 0, cubes = 0, misplaced = 0;
	for (int k = 0; k < 3000; ++k)
	{
		for (uint i = 0; i < COUNTOF(enemy); ++i)
			enemyAvail[i] = 1;   // sweep the last pass's loot so the bank cannot fill up
		enemyAvail[0] = 0;
		enemy[0].ex = (JE_integer)(60 + (k % 100));
		enemy[0].ey = 90;
		enemy[0].linknum = 0;
		enemy_logical_death(0, 0);
		qa_surveyor_count_loot(&bombs, &orbs, &cubes, &misplaced, enemy[0].ex, enemy[0].ey);
	}
	qa_check(bombs > 0 && orbs > 0 && cubes > 0, "with Surveyor all three survey pickups drop");
	qa_check(cubes < bombs + orbs, "the cube drops at longer odds than the survey pair");
	qa_check(misplaced == 0,
	         "every drop is a score item at the kill anchor, outside the freed kill slot");

	// One stack: half the drop chance of two, pinned by comparing the same-size runs.
	endlessPerkTakenBy[0][PERK_SURVEYOR] = 1;
	endlessPerkRederive();
	mt_srand(0x5EED54);
	int bombs1 = 0, orbs1 = 0, cubes1 = 0, misplaced1 = 0;
	for (int k = 0; k < 3000; ++k)
	{
		for (uint i = 0; i < COUNTOF(enemy); ++i)
			enemyAvail[i] = 1;
		enemyAvail[0] = 0;
		enemy[0].linknum = 0;
		enemy_logical_death(0, 0);
		qa_surveyor_count_loot(&bombs1, &orbs1, &cubes1, &misplaced1, enemy[0].ex, enemy[0].ey);
	}
	qa_check(bombs1 + orbs1 + cubes1 > 0 && bombs1 + orbs1 + cubes1 < bombs + orbs + cubes &&
	         misplaced1 == 0,
	         "one stack still drops, at longer odds than two");

	/* A linked hull spends one triple of draws however many parts go down with it, plus the one
	 * scatter draw each spawned pickup costs, and its loot sits at the hull's middle. */
	endlessPerkTakenBy[0][PERK_SURVEYOR] = 2;
	endlessPerkRederive();
	int hullDrops = 0, hullMisplaced = 0, hullBombs = 0, hullOrbs = 0, hullCubes = 0;
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
		hullDrops += qa_surveyor_count_loot(&hullBombs, &hullOrbs, &hullCubes, &hullMisplaced,
		                                    106, 67);
		const Uint32 got = mt_rand();
		mt_srand(markBefore);
		for (int d = 0; d < 3 + hullDrops; ++d)
			mt_rand();
		qa_check(mt_rand() == got,
		         "a linked hull spends one triple of draws plus its spawns' scatter, not one per part");
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
	int coopLoot = 0;
	for (uint i = 0; i < COUNTOF(enemy); ++i)
		coopLoot += enemyAvail[i] == 2;
	const Uint32 gotCoop = mt_rand();
	mt_srand(0x5EED55);
	for (int d = 0; d < 3 + coopLoot; ++d)
		mt_rand();
	qa_check(mt_rand() == gotCoop, "...the holder's kills roll");

	for (uint i = 0; i < COUNTOF(enemy); ++i)
		enemyAvail[i] = 1;
	mt_srand(0x5EED56);
	enemyAvail[5] = 0;
	enemy[5].linknum = 0;
	enemy_logical_death(5, ENDLESS_KILLER_NONE);
	int noneLoot = 0;
	for (uint i = 0; i < COUNTOF(enemy); ++i)
		noneLoot += enemyAvail[i] == 2;
	const Uint32 gotNone = mt_rand();
	mt_srand(0x5EED56);
	for (int d = 0; d < 3 + noneLoot; ++d)
		mt_rand();
	qa_check(mt_rand() == gotNone, "...and an unclaimed kill rolls with the deeper seat");

	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	memcpy(endlessPlayerMods, savedPlayerMods, sizeof(savedPlayerMods));
	endlessActiveMods = savedActive;
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
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

	// An invulnerable ship lands a ram every tenth tick of its window in Endless, never outside it.
	qa_check(endlessRamWhileInvulnerable(20) && endlessRamWhileInvulnerable(10)
	         && !endlessRamWhileInvulnerable(19) && !endlessRamWhileInvulnerable(1),
	         "an invulnerable ship rams on the tick cadence");
	endlessMode = false;
	qa_check(!endlessRamWhileInvulnerable(20), "...and outside Endless never");
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

static bool qa_afterburner_close(float got, float want)
{
	return got > want - 0.01f && got < want + 0.01f;
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
	const float fullBoost = 1.0f + (float)(maxStacks * ENDLESS_PERK_AFTERBURNER_SPEED_PCT) / 100.0f;
	const float fullResist = (float)endlessClamp(maxStacks * ENDLESS_PERK_AFTERBURNER_RESIST_PCT, 0,
	                                             ENDLESS_PERK_AFTERBURNER_RESIST_MAX) / 100.0f;
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

	enum { PULL_TICKS = 100 };
	int plainPulled = 0, resistedPulled = 0;
	endlessPerkSetOwnedFor(0, PERK_AFTERBURNER, 0);
	endlessRollGravityDir();
	for (int tick = 0; tick < PULL_TICKS; ++tick)
		plainPulled += endlessGravityPullY(0);
	endlessPerkSetOwnedFor(0, PERK_AFTERBURNER, maxStacks);
	endlessRollGravityDir();
	for (int tick = 0; tick < PULL_TICKS; ++tick)
		resistedPulled += endlessGravityPullY(0);
	const int wantPulled = (int)((float)plainPulled * (1.0f - fullResist) + 0.5f);
	qa_check(plainPulled > 0 && resistedPulled >= wantPulled - 1 &&
	         resistedPulled <= wantPulled + 1,
	         "Smooth-Motion-off gravity pulls a resisting ship the matching fraction of the way");

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
	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
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

	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
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
