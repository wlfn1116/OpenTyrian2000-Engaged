#include "qa.h"

#include "background.h"
#include "config.h"
#include "debug_flags.h"
#include "endless.h"
#include "enemy_death.h"
#include "episodes.h"
#include "file.h"
#include "game_loop.h"
#include "game_state.h"
#include "health_bars.h"
#include "level_events.h"
#include "level_load.h"
#include "lvllib.h"
#include "mtrand.h"
#include "player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- QA: shipped event scripts --------------------------------------------------------------- */

// A spawn group is the explicitly placed enemies one tick creates through one event type. No piece
// may sit farther than this from its group's median height.
#define QA_SPAWN_SPREAD_PX 100

// Authored groups that place one piece past the spread bound; all are unchanged since Tyrian 2.1.
static const struct
{
	JE_byte episode, lvl_file, type;
	JE_integer enemy;
} qa_authored_spread[] =
{
	{ 2,  8,  6, 810 },  // MARKERS
	{ 3,  6, 15, 707 },  // FLEET
	{ 4, 10,  7, 393 },  // DESERTRUN
};

static const struct
{
	JE_byte episode, lvl_file;
	JE_word time;
} qa_boss_loops[] =
{
	{ 1, 18, 11910 },
	{ 2,  6,  5599 },
	{ 2,  7,  3799 },
	{ 2,  7,  5439 },
	{ 3,  6,  7000 },
	{ 4, 15,  9120 },
	{ 5,  5,  3400 },
};

static unsigned int qa_boss_loops_shipped(void)
{
	unsigned int n = 0;
	for (size_t i = 0; i < COUNTOF(qa_boss_loops); ++i)
		n += episodeAvail[qa_boss_loops[i].episode - 1] ? 1 : 0;
	return n;
}

static void qa_scan_boss_loops(unsigned int *matched, unsigned int *unexpected)
{
	JE_EnemyAvailType savedAvail;
	memcpy(savedAvail, enemyAvail, sizeof(savedAvail));
	const JE_word savedLoc = eventLoc, savedJump = superEnemy254Jump;
	const JE_boolean savedTimer = levelTimer, savedReturn = returnActive;
	memset(enemyAvail, 1, sizeof(enemyAvail));
	levelTimer = false;
	returnActive = false;

	for (JE_word j = 0; j < maxEvent; ++j)
	{
		const struct JE_EventRecType *const e = &eventRec[j];
		if (e->eventtype != 54 || (JE_word)e->eventdat >= e->eventtime)
			continue;

		superEnemy254Jump = 0;
		for (JE_word a = j; a > 0; --a)
			if (eventRec[a - 1].eventtype == 57)
			{
				superEnemy254Jump = (JE_word)eventRec[a - 1].eventdat;
				break;
			}
		eventLoc = j + 1;
		if (!level_loop_lost_super_enemy((JE_word)e->eventdat))
			continue;

		bool known = false;
		for (size_t k = 0; k < COUNTOF(qa_boss_loops); ++k)
			known = known || (qa_boss_loops[k].episode == episodeNum &&
			                  qa_boss_loops[k].lvl_file == lvlFileNum &&
			                  qa_boss_loops[k].time == e->eventtime);
		if (known)
			++*matched;
		else
			++*unexpected;
	}

	memcpy(enemyAvail, savedAvail, sizeof(savedAvail));
	eventLoc = savedLoc;
	superEnemy254Jump = savedJump;
	levelTimer = savedTimer;
	returnActive = savedReturn;
}

static bool qa_spread_authored(const struct JE_EventRecType *e)
{
	for (size_t i = 0; i < COUNTOF(qa_authored_spread); ++i)
	{
		if (qa_authored_spread[i].episode == episodeNum &&
		    qa_authored_spread[i].lvl_file == lvlFileNum &&
		    qa_authored_spread[i].type == e->eventtype &&
		    qa_authored_spread[i].enemy == e->eventdat)
			return true;
	}
	return false;
}

// Count the loaded script's spawns farther than the bound from their group's median height.
static unsigned int qa_scattered_spawns(void)
{
	static const JE_byte spawn_types[] = { 6, 7, 10, 15 };
	static JE_shortint height[EVENT_MAXIMUM];
	unsigned int scattered = 0;

	JE_word start = 0;
	while (start < maxEvent)
	{
		JE_word end = start + 1;
		while (end < maxEvent && eventRec[end].eventtime == eventRec[start].eventtime)
			++end;

		for (size_t t = 0; t < COUNTOF(spawn_types); ++t)
		{
			unsigned int n = 0;
			for (JE_word i = start; i < end; ++i)
			{
				if (eventRec[i].eventtype != spawn_types[t] || eventRec[i].eventdat2 == -99)
					continue;

				JE_word k = (JE_word)n++;
				for (; k > 0 && height[k - 1] > eventRec[i].eventdat5; --k)
					height[k] = height[k - 1];
				height[k] = eventRec[i].eventdat5;
			}
			if (n < 3)
				continue;

			const int median2 = height[(n - 1) / 2] + height[n / 2];  // twice the median
			for (JE_word i = start; i < end; ++i)
			{
				const struct JE_EventRecType *const e = &eventRec[i];
				if (e->eventtype != spawn_types[t] || e->eventdat2 == -99 ||
				    abs(2 * e->eventdat5 - median2) <= 2 * QA_SPAWN_SPREAD_PX ||
				    qa_spread_authored(e))
					continue;

				printf("# scattered spawn: episode %u file %u tick %u enemy %d at y %d\n",
				       (unsigned int)episodeNum, (unsigned int)lvlFileNum,
				       (unsigned int)e->eventtime, (int)e->eventdat, (int)e->eventdat5);
				++scattered;
			}
		}
		start = end;
	}
	return scattered;
}

// Count the loaded pieces of one enemy, returning how many sit at (dx, dy) from a same-tick anchor.
static unsigned int qa_attached_pieces(JE_byte type, JE_integer piece, JE_integer anchor, int dx,
                                       int dy, unsigned int *pieces)
{
	unsigned int attached = 0;
	for (JE_word i = 0; i < maxEvent; ++i)
	{
		const struct JE_EventRecType *const p = &eventRec[i];
		if (p->eventtype != type || p->eventdat != piece)
			continue;

		++*pieces;
		for (JE_word j = 0; j < maxEvent; ++j)
		{
			const struct JE_EventRecType *const a = &eventRec[j];
			if (a->eventtype == type && a->eventdat == anchor && a->eventtime == p->eventtime &&
			    p->eventdat2 == a->eventdat2 + dx && p->eventdat5 == a->eventdat5 + dy)
			{
				++attached;
				break;
			}
		}
	}
	return attached;
}

static int qa_spawn_into_full_bank(bool endless, bool cash)
{
	for (unsigned int i = 0; i < 25; ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = cash ? 2 : 0;
		enemy[i].scoreitem = cash;
		enemy[i].evalue = cash ? 100 : 0;
		enemy[i].ey = (JE_integer)(i * 5);
	}
	if (cash)
	{
		enemy[7].evalue = 5000;
		enemy[7].ey = 200;
		enemy[3].evalue = 1;
		enemy[3].ey = 190;
		enemy[9].evalue = 30001;
		enemy[9].ey = 195;
	}

	memset(eventRec, 0, 2 * sizeof(eventRec[0]));
	eventRec[0].eventtype = 15;
	eventRec[0].eventdat = 1;
	eventRec[0].eventdat2 = 100;
	eventRec[1].eventtime = 65500;
	maxEvent = 1;
	eventLoc = 1;
	endlessMode = endless;
	JE_createNewEventEnemy(0, 0, 0);
	const bool keepsTheRest = !cash || (enemy[7].evalue == 5000 && enemy[3].evalue == 1 &&
	                                    enemy[9].evalue == 30001);
	return keepsTheRest ? (int)b : -1;
}

void qa_test_boosted_boss_pacing(void)
{
	struct JE_EventRecType *const saved = malloc(sizeof(eventRec));
	if (saved == NULL)
	{
		qa_check(false, "the boosted-pacing test can save the loaded event script");
		return;
	}
	memcpy(saved, eventRec, sizeof(eventRec));
	const JE_word savedMax = maxEvent, savedLoc = eventLoc, savedCur = curLoc;
	const JE_word savedBack = backMove, savedBack2 = backMove2, savedBack3 = backMove3;
	const JE_word savedExplode = explodeMove;
	const JE_byte savedDelays[4] = { map1YDelay, map1YDelayMax, map2YDelay, map2YDelayMax };
	const JE_byte savedStopNum = stopBackgroundNum;
	const JE_boolean savedForce = forceEvents;
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const Uint64 savedMods = endlessActiveMods;
	const struct JE_SingleEnemyType savedEnemy = enemy[0];
	JE_byte savedAvail[COUNTOF(enemyAvail)];
	memcpy(savedAvail, enemyAvail, sizeof(savedAvail));
	const boss_bar_t savedBars[2] = { boss_bar[0], boss_bar[1] };

	endlessMode = true;
	endlessCampaignMods = false;
	forceEvents = false;
	boss_bar[0].link_num = 0;
	boss_bar[1].link_num = 0;
	memset(enemyAvail, 1, sizeof(savedAvail));

	memset(eventRec, 0, 3 * sizeof(eventRec[0]));
	eventRec[0].eventtime = 100;
	eventRec[0].eventtype = 2;
	eventRec[1].eventtime = 101;
	eventRec[1].eventtype = 2;
	eventRec[1].eventdat = 1;
	eventRec[2].eventtime = 65500;
	maxEvent = 2;

	bool held[2];
	for (int boosted = 0; boosted <= 1; ++boosted)
	{
		endlessActiveMods = boosted ? ENDLESS_MOD_WARP : 0;
		event_grid_rebase(0, 1);
		event_scroll_span_set(97, 103);
		eventLoc = 1;
		curLoc = 103;
		while (eventRec[eventLoc - 1].eventtime <= curLoc && eventLoc <= maxEvent)
			JE_eventSystem();
		held[boosted] = curLoc == 100 && backMove == 0 && eventLoc == 2;
	}
	qa_check(held[1] && !held[0],
	         "under a scroll boost a map stop holds the event clock where it stops, as at 1x");

	eventRec[1].eventtime = 150;
	event_grid_rebase(0, 1);
	event_scroll_span_set(99, 100);
	eventLoc = 1;
	curLoc = 150;
	while (eventRec[eventLoc - 1].eventtime <= curLoc && eventLoc <= maxEvent)
		JE_eventSystem();
	qa_check(curLoc == 150 && backMove == 1 && eventLoc == 3,
	         "...but a jump past the stop re-reads it without pulling the clock back");
	event_scroll_span_set(0, 0);

	memset(eventRec, 0, 3 * sizeof(eventRec[0]));
	eventRec[0].eventtime = 50;
	eventRec[0].eventtype = 7;
	eventRec[0].eventdat4 = 9;
	eventRec[1].eventtime = 100;
	eventRec[1].eventtype = 79;
	eventRec[1].eventdat = 9;
	eventRec[2].eventtime = 65500;
	maxEvent = 2;
	level_scan_boss_leadins();
	endlessActiveMods = ENDLESS_MOD_WARP;
	enemy[0].linknum = 9;
	enemyAvail[0] = 0;
	curLoc = 60;
	const int approaching = endlessScrollBoostPercent();
	curLoc = 40;
	const int early = endlessScrollBoostPercent();
	curLoc = 60;
	enemyAvail[0] = 1;
	const int gone = endlessScrollBoostPercent();
	curLoc = 45;
	const int runup = endlessScrollBoostPercent();
	qa_check(approaching == 0 && early == 220 && gone == 220,
	         "a boss's approach holds a boosted scroll from its spawn while any of it lives");
	qa_check(runup == 0,
	         "...and the last units before the spawn run at 1x, so it lands on the authored tick");

	struct JE_SingleEnemyType savedBank[25];
	memcpy(savedBank, enemy, sizeof(savedBank));
	const int reclaimed = qa_spawn_into_full_bank(true, true);
	const int campaign = qa_spawn_into_full_bank(false, true);
	const int crowded = qa_spawn_into_full_bank(true, false);
	memcpy(enemy, savedBank, sizeof(savedBank));
	qa_check(reclaimed == 25 && campaign == 0 && crowded == 0,
	         "a scripted spawn into a full bank takes the cheapest, lowest coin's slot in "
	         "Endless, never a gem, cube or power-up; the campaign keeps the vanilla drop");

	memcpy(eventRec, saved, sizeof(eventRec));
	free(saved);
	maxEvent = savedMax;
	level_scan_boss_leadins();
	eventLoc = savedLoc;
	curLoc = savedCur;
	backMove = savedBack;
	backMove2 = savedBack2;
	backMove3 = savedBack3;
	explodeMove = savedExplode;
	map1YDelay = savedDelays[0];
	map1YDelayMax = savedDelays[1];
	map2YDelay = savedDelays[2];
	map2YDelayMax = savedDelays[3];
	stopBackgroundNum = savedStopNum;
	forceEvents = savedForce;
	endlessActiveMods = savedMods;
	endlessCampaignMods = savedCampaign;
	endlessMode = savedEndless;
	enemy[0] = savedEnemy;
	memcpy(enemyAvail, savedAvail, sizeof(savedAvail));
	boss_bar[0] = savedBars[0];
	boss_bar[1] = savedBars[1];
	event_grid_rebase(0, 1);
}

static int qa_landing(JE_word at, int extra, JE_word back)
{
	curLoc = at;
	backMove = back;
	eventLoc = 1;
	return event_scroll_landing(extra);
}

static bool qa_leadin_holds(JE_word loc, JE_byte link);

void qa_test_boosted_scroll_landing(void)
{
	struct JE_EventRecType *const saved = malloc(sizeof(eventRec));
	if (saved == NULL)
	{
		qa_check(false, "the scroll-landing test can save the loaded event script");
		return;
	}
	memcpy(saved, eventRec, sizeof(eventRec));
	const JE_word savedMax = maxEvent, savedLoc = eventLoc, savedCur = curLoc;
	const JE_word savedBack = backMove, savedBack2 = backMove2, savedBack3 = backMove3;
	const JE_word savedExplode = explodeMove;
	const JE_byte savedDelays[4] = { map1YDelay, map1YDelayMax, map2YDelay, map2YDelayMax };
	const JE_byte savedStopNum = stopBackgroundNum;
	const JE_boolean savedForce = forceEvents;
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const Uint64 savedMods = endlessActiveMods;
	const struct JE_SingleEnemyType savedEnemy = enemy[0];
	JE_byte savedAvail[COUNTOF(enemyAvail)];
	memcpy(savedAvail, enemyAvail, sizeof(savedAvail));
	const boss_bar_t savedBars[2] = { boss_bar[0], boss_bar[1] };

	endlessMode = true;
	endlessCampaignMods = false;
	forceEvents = false;
	boss_bar[0].link_num = 0;
	boss_bar[1].link_num = 0;
	memset(enemyAvail, 1, sizeof(savedAvail));
	map1YDelayMax = 1;
	endlessActiveMods = ENDLESS_MOD_WARP;
	event_grid_rebase(0, 1);

	memset(eventRec, 0, 6 * sizeof(eventRec[0]));
	eventRec[0].eventtime = 100;
	eventRec[0].eventtype = 2;
	eventRec[1].eventtime = 65500;
	maxEvent = 1;
	level_scan_boss_leadins();

	qa_check(qa_landing(98, 4, 1) == 2 && qa_landing(100, 3, 1) == 0 && qa_landing(90, 4, 1) == 4,
	         "a boosted tick lands the clock on a map stop it would cross, and adds nothing once "
	         "the base step has reached it");
	qa_check(qa_landing(97, 2, 2) == 1 && qa_landing(96, 2, 2) == 2,
	         "...and stops a base step short when the next base step would carry it past");

	eventRec[0].eventdat = 1;
	qa_check(qa_landing(98, 4, 1) == 2 && qa_landing(97, 2, 2) == 2,
	         "a speed change is landed on the same way, without the base-step lead");

	eventRec[0].eventdat = 0;
	eventRec[2] = eventRec[1];
	eventRec[1] = eventRec[0];
	eventRec[0].eventtime = 99;
	eventRec[0].eventtype = 54;
	eventRec[0].eventdat = 500;
	maxEvent = 2;
	qa_check(qa_landing(98, 4, 1) == 4, "a jump ahead of the stop leaves the boost alone");
	eventRec[0] = eventRec[1];
	eventRec[1] = eventRec[2];
	maxEvent = 1;

	forceEvents = true;
	const int forced = qa_landing(98, 4, 1);
	forceEvents = false;
	endlessActiveMods = 0;
	const int unboosted = qa_landing(98, 4, 1);
	endlessActiveMods = ENDLESS_MOD_WARP;
	qa_check(forced == 4 && unboosted == 4,
	         "forced events and an unboosted scroll keep their extra pixels untouched");

	event_grid_rebase(1975, 2);
	qa_check(event_grid_point(2176) == 2177 && event_grid_point(2177) == 2177 &&
	         event_grid_point(1970) == 1975 && event_grid_point(1976) == 1977,
	         "on a two-pixel 1x step an event fires at the first clock the 1x step reaches");
	eventRec[0].eventtime = 2176;
	qa_check(qa_landing(2174, 3, 2) == 3,
	         "...so a boosted tick lands on the stop where the 1x clock would stop");

	event_scroll_span_set(2170, 2180);
	curLoc = 2180;
	eventLoc = 1;
	backMove = 2;
	while (eventRec[eventLoc - 1].eventtime <= curLoc && eventLoc <= maxEvent)
		JE_eventSystem();
	qa_check(curLoc == 2177 && backMove == 0,
	         "...and a boosted overshoot is held there, not pulled back to the authored tick");
	event_scroll_span_set(0, 0);
	event_grid_rebase(0, 1);

	float frac1 = 0.0f, frac3 = 0.0f;
	endlessActiveMods = ENDLESS_MOD_SLIPSTREAM;
	endlessScrollExtraPx(0, 0, 1, 0, NULL, NULL);
	endlessScrollExtraPx(2, 0, 1, 0, NULL, NULL);
	int sum1 = 0;
	bool lockstep = true;
	for (int t = 0; t < 40; ++t)
	{
		const int extra1 = endlessScrollExtraPx(0, 1, 1, 1, NULL, &frac1);
		endlessScrollExtraPx(2, 3, 1, 3, NULL, NULL);
		const int extra3 = endlessScrollLinkLayer3(extra1, 3, &frac3);
		const float drift = frac3 - 3.0f * frac1;
		sum1 += extra1;
		lockstep = lockstep && extra3 == 3 * extra1 && drift < 0.001f && drift > -0.001f;
	}
	endlessScrollExtraPx(0, 0, 1, 0, NULL, NULL);
	endlessScrollExtraPx(2, 0, 1, 0, NULL, NULL);
	qa_check(lockstep && sum1 > 0,
	         "layer 3 at three times layer 1's speed takes three boost pixels for each of layer "
	         "1's, so the two layers keep their 1x phase");

	memset(eventRec, 0, 6 * sizeof(eventRec[0]));
	eventRec[0].eventtime = 50;
	eventRec[0].eventtype = 7;
	eventRec[0].eventdat4 = 17;
	eventRec[1].eventtime = 55;
	eventRec[1].eventtype = 7;
	eventRec[1].eventdat4 = 19;
	eventRec[2].eventtime = 60;
	eventRec[2].eventtype = 39;
	eventRec[2].eventdat = 17;
	eventRec[2].eventdat2 = 19;
	eventRec[3].eventtime = 70;
	eventRec[3].eventtype = 39;
	eventRec[3].eventdat = 19;
	eventRec[3].eventdat2 = 254;
	eventRec[4].eventtime = 100;
	eventRec[4].eventtype = 79;
	eventRec[4].eventdat = 254;
	eventRec[5].eventtime = 65500;
	maxEvent = 5;
	level_scan_boss_leadins();
	curLoc = 80;
	qa_check(level_boss_leadin_window(17) >= 0 && level_boss_leadin_window(19) >= 0 &&
	         level_boss_leadin_window(254) >= 0 && level_boss_leadin_window(18) < 0,
	         "a boss assembled through a chain of relinks frames every piece that ends up in it");
	qa_check(qa_leadin_holds(80, 19) && !qa_leadin_holds(80, 17),
	         "...while the boosted-scroll hold keeps its one-step link set");

	memcpy(eventRec, saved, sizeof(eventRec));
	free(saved);
	maxEvent = savedMax;
	level_scan_boss_leadins();
	eventLoc = savedLoc;
	curLoc = savedCur;
	backMove = savedBack;
	backMove2 = savedBack2;
	backMove3 = savedBack3;
	explodeMove = savedExplode;
	map1YDelay = savedDelays[0];
	map1YDelayMax = savedDelays[1];
	map2YDelay = savedDelays[2];
	map2YDelayMax = savedDelays[3];
	stopBackgroundNum = savedStopNum;
	forceEvents = savedForce;
	endlessActiveMods = savedMods;
	endlessCampaignMods = savedCampaign;
	endlessMode = savedEndless;
	enemy[0] = savedEnemy;
	memcpy(enemyAvail, savedAvail, sizeof(savedAvail));
	boss_bar[0] = savedBars[0];
	boss_bar[1] = savedBars[1];
}

static bool qa_leadin_holds(JE_word loc, JE_byte link)
{
	const JE_word savedLoc = curLoc;
	const struct JE_SingleEnemyType savedEnemy = enemy[0];
	const JE_byte savedAvail = enemyAvail[0];

	curLoc = loc;
	enemy[0].linknum = link;
	enemyAvail[0] = 0;
	const bool holds = level_boss_leadin_active();

	enemyAvail[0] = savedAvail;
	enemy[0] = savedEnemy;
	curLoc = savedLoc;
	return holds;
}

static bool qa_gryphon_bar_follows_head(void)
{
	const struct JE_EventRecType *bar = NULL;
	for (JE_word i = 0; i < maxEvent && bar == NULL; ++i)
		if (eventRec[i].eventtype == 79)
			bar = &eventRec[i];
	if (bar == NULL)
		return false;

	const boss_bar_t savedBars[2] = { boss_bar[0], boss_bar[1] };
	const JE_byte savedBody = boss_body_link;
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedExpert = expertMode;
	const int savedDepth = endlessRunDepth;
	struct JE_SingleEnemyType savedEnemy[3];
	memcpy(savedEnemy, enemy, sizeof(savedEnemy));

	endlessMode = false;
	endlessCampaignMods = false;
	expertMode = false;
	level_boss_bar_event(bar);
	const bool stock = boss_bar[0].link_num == 61 && boss_bar[1].link_num == 0 &&
	                   boss_body_link == 0;

	endlessMode = true;
	endlessRunDepth = 99;
	level_boss_bar_event(bar);
	const bool head = boss_bar[0].link_num == 62 && boss_bar[1].link_num == 0 &&
	                  boss_body_link == 61 && !enemy_has_boss_bar(61);
	static const JE_byte links[3] = { 61, 62, 5 };
	for (int i = 0; i < 3; ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemy[i].linknum = links[i];
		enemy[i].eliteState = 1;
	}
	const bool bodyScaled = enemy_hp_multiplier(0) == enemy_hp_multiplier(1) &&
	                        enemy_hp_multiplier(1) > enemy_hp_multiplier(2);

	memcpy(enemy, savedEnemy, sizeof(savedEnemy));
	endlessRunDepth = savedDepth;
	expertMode = savedExpert;
	endlessCampaignMods = savedCampaign;
	endlessMode = savedEndless;
	boss_body_link = savedBody;
	boss_bar[0] = savedBars[0];
	boss_bar[1] = savedBars[1];
	return stock && head && bodyScaled;
}

static JE_MultiEnemyType qaArmorEnemies;
static JE_EnemyAvailType qaArmorAvail;
static boss_bar_t qaArmorBars[2];
static JE_byte qaArmorBody;
static JE_boolean qaArmorEndless, qaArmorCampaign;
static int qaArmorCue;

static void qa_armor_scene_enter(bool endless)
{
	memcpy(qaArmorEnemies, enemy, sizeof(enemy));
	memcpy(qaArmorAvail, enemyAvail, sizeof(enemyAvail));
	qaArmorBars[0] = boss_bar[0];
	qaArmorBars[1] = boss_bar[1];
	qaArmorBody = boss_body_link;
	qaArmorEndless = endlessMode;
	qaArmorCampaign = endlessCampaignMods;
	qaArmorCue = vulnerableCue;

	endlessMode = endless;
	endlessCampaignMods = false;
	vulnerableCue = VULN_CUE_OFF;
	boss_bar[0].link_num = 0;
	boss_bar[1].link_num = 0;
	boss_body_link = 0;
	qa_enemy_clear_all();
}

static void qa_armor_scene_leave(void)
{
	vulnerableCue = qaArmorCue;
	endlessCampaignMods = qaArmorCampaign;
	endlessMode = qaArmorEndless;
	boss_body_link = qaArmorBody;
	boss_bar[0] = qaArmorBars[0];
	boss_bar[1] = qaArmorBars[1];
	memcpy(enemyAvail, qaArmorAvail, sizeof(enemyAvail));
	memcpy(enemy, qaArmorEnemies, sizeof(enemy));
}

static int qa_boss_armor_heals(JE_byte link, bool endless, int *wounds)
{
	qa_armor_scene_enter(endless);
	qa_enemy_stage(0, 100, 50, 60);
	enemy[0].linknum = link;
	enemy_note_full_armor(&enemy[0]);

	int heals = 0;
	*wounds = 0;
	for (JE_word i = 0; i < maxEvent; ++i)
	{
		const struct JE_EventRecType *const ev = &eventRec[i];
		if (ev->eventtype == 79)
			level_boss_bar_event(ev);
		if ((ev->eventtype != 25 && ev->eventtype != 47) ||
		    (ev->eventdat4 != 0 && ev->eventdat4 != link))
			continue;

		const JE_byte before = enemy[0].armorleft;
		const bool boss = enemy_has_boss_hp(link);
		level_armor_event(ev->eventdat4, (JE_byte)ev->eventdat);
		if (boss && before < 255 && enemy[0].armorleft < 255 && enemy[0].armorleft > before)
			++heals;
		if (enemy[0].armorleft < 255 && enemy[0].armorleft > 30)
		{
			enemy[0].armorleft -= 30;
			enemy[0].healthbar_seen = true;
			++*wounds;
		}
	}

	qa_armor_scene_leave();
	return heals;
}

static bool qa_boss_keeps_wounds(JE_byte link, int stockHeals)
{
	int stockWounds = 0, endlessWounds = 0;
	const int stock = qa_boss_armor_heals(link, false, &stockWounds);
	const int endless = qa_boss_armor_heals(link, true, &endlessWounds);
	return stock == stockHeals && endless == 0 && endlessWounds == stockWounds;
}

static bool qa_rearm_rules(void)
{
	qa_armor_scene_enter(true);
	boss_bar[0].link_num = 7;
	qa_enemy_stage(0, 60, 50, 100);
	qa_enemy_stage(1, 120, 50, 100);
	enemy[0].linknum = 7;
	enemy[1].linknum = 9;
	for (unsigned int i = 0; i < 2; ++i)
	{
		enemy_note_full_armor(&enemy[i]);
		enemy[i].armorleft = 40;
		enemy[i].healthbar_seen = true;
	}

	level_armor_event(0, 100);
	const bool kept = enemy[0].armorleft == 40 && enemy[1].armorleft == 100;
	level_armor_event(7, 200);
	const bool bigger = enemy[0].armorleft == 200;
	enemy[0].armorleft = 150;
	level_armor_event(7, 255);
	const bool sealed = enemy[0].armorleft == 255;
	level_armor_event(7, 120);
	const bool opened = enemy[0].armorleft == 120;

	qa_armor_scene_leave();
	return kept && bigger && sealed && opened;
}

void qa_test_level_event_scripts(void)
{
	struct JE_EventRecType *const saved = malloc(sizeof(eventRec));
	if (saved == NULL)
	{
		qa_check(false, "the level event test can save the loaded event script");
		return;
	}
	memcpy(saved, eventRec, sizeof(eventRec));
	const JE_word saved_events = maxEvent;
	const JE_byte saved_episode = episodeNum, saved_lvl_file = lvlFileNum;

	unsigned int levels = 0, nubs = 0, attached = 0, scattered = 0, turrets = 0, stacked = 0;
	unsigned int bossLoops = 0, strayLoops = 0;
	bool read_all = true, gryphon = false, harvest = false, underdeli = false;
	bool fleet = false, core = false;
	for (int ep = 1; ep <= EPISODE_MAX; ++ep)
	{
		if (!episodeAvail[ep - 1])
			continue;

		char name[13];
		snprintf(name, sizeof(name), "tyrian%d.lvl", ep);
		FILE *const f = dir_fopen_warn(data_dir(), name, "rb");
		JE_LvlPosType pos;
		JE_word offsets = 0;
		if (f != NULL)
			fread_u16_die(&offsets, 1, f);
		if (f == NULL || offsets < 3 || offsets >= COUNTOF(pos) || offsets % 2 == 0)
		{
			read_all = false;
			if (f != NULL)
				fclose(f);
			continue;
		}
		fread_s32_die(pos, offsets, f);

		episodeNum = (JE_byte)ep;
		for (lvlFileNum = 1; lvlFileNum <= offsets / 2; ++lvlFileNum)
		{
			// Skip what JE_loadMap reads first: two file letters, three map origins, then the
			// random-enemy list.
			JE_word enemies;
			if (fseek(f, pos[(lvlFileNum - 1) * 2] + 8, SEEK_SET) != 0)
			{
				read_all = false;
				break;
			}
			fread_u16_die(&enemies, 1, f);
			if (fseek(f, 2L * enemies, SEEK_CUR) != 0 || !level_read_event_script(f))
			{
				read_all = false;
				continue;
			}

			++levels;
			scattered += qa_scattered_spawns();
			qa_scan_boss_loops(&bossLoops, &strayLoops);
			stacked += qa_attached_pieces(6, 171, 172, 0, -28, &turrets);
			if (ep == 4 && lvlFileNum == 4)
				attached += qa_attached_pieces(7, 212, 213, -18, 6, &nubs);
			if (ep == 2 && lvlFileNum == 6)
				gryphon = qa_gryphon_bar_follows_head();
			if (ep == 4 && lvlFileNum == 1)
				harvest = qa_leadin_holds(6200, 10) && !qa_leadin_holds(5000, 10) &&
				          !qa_leadin_holds(6400, 10);
			if (ep == 4 && lvlFileNum == 7)
				underdeli = qa_leadin_holds(4700, 18) && qa_leadin_holds(5500, 17) &&
				            !qa_leadin_holds(4500, 17);
			if (ep == 3 && lvlFileNum == 6)
				fleet = qa_boss_keeps_wounds(254, 5);
			if (ep == 4 && lvlFileNum == 5)
				core = qa_boss_keeps_wounds(199, 2);
		}
		fclose(f);
	}

	memcpy(eventRec, saved, sizeof(eventRec));
	free(saved);
	maxEvent = saved_events;
	episodeNum = saved_episode;
	lvlFileNum = saved_lvl_file;
	level_scan_boss_leadins();

	qa_check(read_all && levels > 0, "every shipped level's event script reads through the loader");
	qa_check(!episodeAvail[4 - 1] || (harvest && underdeli),
	         "a boosted scroll holds from a boss's spawn: HARVEST's pieces, both UNDERDELI bosses");
	qa_check(!episodeAvail[4 - 1] || (nubs > 0 && attached == nubs),
	         "SURFACE's arch nubs spawn beside their arches, as in Tyrian 2.1");
	qa_check(!episodeAvail[1 - 1] || (turrets > 0 && stacked == turrets),
	         "every Savara cannon top sits on its tower, SAVARA IV's included, as in Tyrian 2.1");
	qa_check(scattered == 0, "outside the authored designs, no spawn group places a piece over "
	         "100px from its median height");
	qa_check(bossLoops == qa_boss_loops_shipped() && strayLoops == 0,
	         "a loop that waits on a 254 enemy that is gone takes its exit on exactly the shipped "
	         "boss waits (ALE, GRYPHON, GEM WAR twice, FLEET, BRAINIAC, ASTEROIDS) and nowhere "
	         "else");
	qa_check(!episodeAvail[2 - 1] || gryphon,
	         "GRYPHON keeps one bar: the body's in the campaign, the head's under scaled boss "
	         "HP, with the body still on boss HP");
	qa_check(!episodeAvail[3 - 1] || fleet,
	         "FLEET's boss keeps its wounds through its five re-arms in Endless; the campaign "
	         "heals them");
	qa_check(!episodeAvail[4 - 1] || core,
	         "CORE's boss keeps its wounds through its two mid-fight re-arms in Endless");
	qa_check(qa_rearm_rules(),
	         "an Endless re-arm spares a wounded boss part but still re-arms ordinary enemies, "
	         "seals at 255, opens from it and sets a larger pool");
	printf("# level events: %u levels, %u SURFACE arch nubs, %u cannon tops, %u scattered spawns\n",
	       levels, nubs, turrets, scattered);
}

static void qa_loop_script(void)
{
	memset(eventRec, 0, 6 * sizeof(eventRec[0]));
	eventRec[0].eventtime = 10;
	eventRec[0].eventtype = 57;
	eventRec[0].eventdat = 100;
	eventRec[1].eventtime = 20;
	eventRec[1].eventtype = 7;
	eventRec[1].eventdat4 = 254;
	eventRec[2].eventtime = 30;
	eventRec[2].eventtype = 9;
	eventRec[3].eventtime = 50;
	eventRec[3].eventtype = 54;
	eventRec[3].eventdat = 30;
	eventRec[4].eventtime = 100;
	eventRec[4].eventtype = 9;
	eventRec[5].eventtime = 65500;
	maxEvent = 5;
	superEnemy254Jump = 100;
	levelTimer = false;
	returnActive = false;
	memset(enemyAvail, 1, sizeof(enemyAvail));
}

static JE_word qa_loop_run(void)
{
	eventLoc = 4;
	curLoc = 50;
	JE_eventSystem();
	return curLoc;
}

void qa_test_super_enemy_loops(void)
{
	struct JE_EventRecType *const saved = malloc(sizeof(eventRec));
	if (saved == NULL)
	{
		qa_check(false, "the script-loop test can save the loaded event script");
		return;
	}
	memcpy(saved, eventRec, sizeof(eventRec));
	static JE_MultiEnemyType savedEnemies;
	memcpy(savedEnemies, enemy, sizeof(enemy));
	JE_EnemyAvailType savedAvail;
	memcpy(savedAvail, enemyAvail, sizeof(savedAvail));
	JE_byte savedPL[COUNTOF(newPL)];
	memcpy(savedPL, newPL, sizeof(savedPL));
	const JE_word savedMax = maxEvent, savedLoc = eventLoc, savedCur = curLoc;
	const JE_word savedJump = superEnemy254Jump, savedCountdown = levelTimerCountdown;
	const JE_boolean savedTimer = levelTimer, savedReturn = returnActive;

	qa_loop_script();
	qa_enemy_stage(0, 100, 50, 20);
	enemy[0].linknum = 254;
	const bool waits = qa_loop_run() == 30 && eventLoc == 3 && superEnemy254Jump == 100;
	qa_loop_script();
	const bool exits = qa_loop_run() == 100 && eventLoc == 5 && superEnemy254Jump == 0;
	qa_loop_script();
	enemyAvail[0] = 2;
	const bool wreck = qa_loop_run() == 100;
	qa_loop_script();
	eventRec[6] = eventRec[5];
	eventRec[5] = eventRec[4];
	eventRec[4].eventtime = 99;
	maxEvent = 6;
	const bool likeKill = qa_loop_run() == 100 && eventRec[eventLoc - 1].eventtime == 99;
	qa_check(waits && exits && wreck && likeKill,
	         "a script loop waiting on its 254 enemy loops while it lives and takes the armed exit "
	         "once it is gone, a wreck included, running the event before the exit as a kill "
	         "would unless that event is the loop's own jump back");

	bool held = true;
	qa_loop_script();
	eventRec[2].eventtype = 7;
	eventRec[2].eventdat4 = 254;
	held = held && qa_loop_run() == 30;
	qa_loop_script();
	eventRec[2].eventtype = 39;
	eventRec[2].eventdat2 = 254;
	held = held && qa_loop_run() == 30;
	qa_loop_script();
	eventRec[2].eventtype = 70;
	eventRec[2].eventdat = 200;
	held = held && qa_loop_run() == 30;
	qa_loop_script();
	eventRec[2].eventtype = 63;
	eventRec[2].eventdat = 1;
	held = held && qa_loop_run() == 30;
	qa_loop_script();
	eventRec[0].eventdat = 40;
	superEnemy254Jump = 40;
	held = held && qa_loop_run() == 30;
	qa_loop_script();
	eventRec[0].eventtype = 9;
	eventRec[4].eventtype = 57;
	eventRec[4].eventdat = 100;
	held = held && qa_loop_run() == 30;
	qa_loop_script();
	levelTimer = true;
	levelTimerCountdown = 5;
	held = held && qa_loop_run() == 30;
	qa_loop_script();
	returnActive = true;
	held = held && qa_loop_run() == 30;
	qa_check(held,
	         "...but loops on while its body can spawn or retag a 254 enemy, has another way out, "
	         "a timer or return is pending, or the armed exit belongs to another stretch");

	const JE_word savedBack = backMove;
	const JE_byte savedDelayMax = map1YDelayMax, savedStopNum = stopBackgroundNum;
	const JE_boolean savedForce = forceEvents, savedReady = readyToEndLevel, savedEnd = endLevel;
	qa_loop_script();
	backMove = 0;
	map1YDelayMax = 1;
	forceEvents = false;
	stopBackgroundNum = 0;
	readyToEndLevel = false;
	endLevel = false;
	eventLoc = 3;
	curLoc = 30;
	qa_enemy_stage(0, 100, 50, 20);
	enemy[0].linknum = 254;
	const bool aliveHolds = !level_stall_lost_super_enemy();
	enemyAvail[0] = 1;
	const bool stranded = level_stall_lost_super_enemy();
	bool moving = true;
	forceEvents = true;
	moving = moving && !level_stall_lost_super_enemy();
	forceEvents = false;
	backMove = 1;
	moving = moving && !level_stall_lost_super_enemy();
	backMove = 0;
	stopBackgroundNum = 1;
	moving = moving && !level_stall_lost_super_enemy();
	stopBackgroundNum = 0;
	readyToEndLevel = true;
	moving = moving && !level_stall_lost_super_enemy();
	readyToEndLevel = false;
	superEnemy254Jump = 20;
	moving = moving && !level_stall_lost_super_enemy();
	superEnemy254Jump = 100;
	level_super_enemy_down(254);
	const bool resumed = curLoc == 100 && superEnemy254Jump == 0;
	backMove = savedBack;
	map1YDelayMax = savedDelayMax;
	stopBackgroundNum = savedStopNum;
	forceEvents = savedForce;
	readyToEndLevel = savedReady;
	endLevel = savedEnd;
	qa_check(aliveHolds && stranded && moving && resumed,
	         "a frozen map stop waiting on a 254 enemy that is gone takes the armed exit, but not "
	         "while one lives, the clock still runs, a stop resumes itself or the level can end");

	memset(eventRec, 0, 3 * sizeof(eventRec[0]));
	eventRec[0].eventtime = 10;
	eventRec[0].eventtype = 75;
	eventRec[0].eventdat = 5;
	eventRec[0].eventdat2 = 5;
	eventRec[0].eventdat3 = 80;
	eventRec[1].eventtime = 65500;
	maxEvent = 1;
	qa_enemy_clear_all();
	qa_enemy_stage(3, 100, 50, 10);
	qa_enemy_stage(7, 120, 50, 10);
	enemy[3].linknum = 5;
	enemy[7].linknum = 5;
	enemy[7].eyc = 1;
	newPL[0] = 0;
	eventLoc = 1;
	curLoc = 10;
	JE_eventSystem();
	const bool noPick = newPL[0] == 255;
	enemy[7].eyc = 0;
	eventLoc = 1;
	JE_eventSystem();
	const bool picked = newPL[0] == 5;
	JE_byte before[COUNTOF(newPL)];
	memcpy(before, newPL, sizeof(before));
	eventRec[0].eventdat3 = 95;
	eventLoc = 1;
	JE_eventSystem();
	const bool contained = memcmp(before, newPL, sizeof(before)) == 0;
	qa_check(noPick && picked && contained,
	         "a random-link pick returns when no link's last live enemy stands still, picks one "
	         "that does, and never writes outside its ten registers");

	memset(eventRec, 0, 8 * sizeof(eventRec[0]));
	eventRec[0].eventtime = 10;
	eventRec[1].eventtime = 20;
	eventRec[2].eventtime = 65500;
	maxEvent = 2;
	eventLoc = 1;
	curLoc = 10;
	JE_eventJump(65510);
	qa_check(eventLoc == maxEvent,
	         "a jump past the last event parks on the sentinel instead of running off the script");

	memcpy(eventRec, saved, sizeof(eventRec));
	free(saved);
	memcpy(enemy, savedEnemies, sizeof(enemy));
	memcpy(enemyAvail, savedAvail, sizeof(savedAvail));
	memcpy(newPL, savedPL, sizeof(savedPL));
	maxEvent = savedMax;
	eventLoc = savedLoc;
	curLoc = savedCur;
	superEnemy254Jump = savedJump;
	levelTimer = savedTimer;
	levelTimerCountdown = savedCountdown;
	returnActive = savedReturn;
}

static int qa_piece_spawn(JE_word event)
{
	eventLoc = event + 1;
	b = 0;
	JE_eventSystem();
	const int slot = b;
	if (slot > 0)
		enemyAvail[slot - 1] = 1;
	return slot;
}

static void qa_piece_live(unsigned int slot, JE_word type, int x)
{
	memset(&enemy[slot], 0, sizeof(enemy[slot]));
	enemyAvail[slot] = 0;
	enemy[slot].linknum = 11;
	enemy[slot].enemytype = type;
	enemy[slot].armorleft = 40;
	enemy[slot].xaccel = 96;
	enemy[slot].yaccel = 96;
	enemy[slot].ex = (JE_integer)x;
	enemy[slot].ey = 40;
}

void qa_test_stacked_pieces(void)
{
	struct JE_EventRecType *const savedEvents = malloc(sizeof(eventRec));
	struct JE_SingleEnemyType *const savedEnemy = malloc(sizeof(enemy));
	void *const savedRng = malloc(mt_state_size());
	if (savedEvents == NULL || savedEnemy == NULL || savedRng == NULL)
	{
		free(savedEvents);
		free(savedEnemy);
		free(savedRng);
		qa_check(false, "the stacked-piece test can save the event script and enemy pool");
		return;
	}
	memcpy(savedEvents, eventRec, sizeof(eventRec));
	memcpy(savedEnemy, enemy, sizeof(enemy));
	mt_state_save(savedRng);
	JE_byte savedAvail[COUNTOF(enemyAvail)];
	memcpy(savedAvail, enemyAvail, sizeof(savedAvail));
	enum { TOP = 426, BOTTOM = 428, SIDE = 429 };
	unsigned char savedDat[4][sizeof(enemyDat[0])];
	memcpy(savedDat[0], &enemyDat[TOP], sizeof(enemyDat[0]));
	memcpy(savedDat[1], &enemyDat[BOTTOM], sizeof(enemyDat[0]));
	memcpy(savedDat[2], &enemyDat[0], sizeof(enemyDat[0]));
	memcpy(savedDat[3], &enemyDat[SIDE], sizeof(enemyDat[0]));
	const JE_word savedMax = maxEvent, savedLoc = eventLoc, savedCur = curLoc;
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const Uint64 savedMods = endlessActiveMods;
	const boss_bar_t savedBars[2] = { boss_bar[0], boss_bar[1] };
	const JE_byte savedBody = boss_body_link;
	const JE_integer savedX = player[0].x, savedY = player[0].y;

	enemyDat[TOP].esize = 1;
	enemyDat[TOP].egraphic[0] = 153;
	enemyDat[BOTTOM].esize = 1;
	enemyDat[BOTTOM].egraphic[0] = 191;
	enemyDat[0].esize = 1;
	enemyDat[SIDE].esize = 1;
	enemyDat[SIDE].egraphic[0] = 193;
	endlessMode = true;
	endlessCampaignMods = false;
	endlessActiveMods = 0;
	boss_bar[0].link_num = 0;
	boss_bar[1].link_num = 0;
	boss_body_link = 0;
	memset(enemyAvail, 1, sizeof(savedAvail));
	memset(eventRec, 0, 3 * sizeof(eventRec[0]));
	eventRec[0] = (struct JE_EventRecType){ .eventtime = 100, .eventtype = 10, .eventdat = BOTTOM,
	                                        .eventdat2 = 150, .eventdat4 = 11 };
	eventRec[1] = (struct JE_EventRecType){ .eventtime = 128, .eventtype = 10, .eventdat = TOP,
	                                        .eventdat2 = 150, .eventdat4 = 11 };
	eventRec[2].eventtime = 65500;
	maxEvent = 2;
	level_scan_boss_leadins();

	bool rule = level_piece_orphaned(1) && !level_piece_orphaned(0);
	qa_piece_live(75, BOTTOM, 150);
	rule = rule && !level_piece_orphaned(1);
	enemyAvail[75] = 1;
	eventRec[1].eventdat = BOTTOM;
	rule = rule && !level_piece_orphaned(1);
	eventRec[1].eventdat = TOP;
	eventRec[1].eventdat2 = 174;
	rule = rule && !level_piece_orphaned(1);
	eventRec[1].eventdat2 = 150;
	eventRec[1].eventtime = 161;
	rule = rule && !level_piece_orphaned(1);
	eventRec[1].eventtime = 128;
	boss_bar[0].link_num = 11;
	rule = rule && !level_piece_orphaned(1);
	boss_bar[0].link_num = 0;
	eventRec[3] = eventRec[2];
	eventRec[2] = eventRec[1];
	eventRec[1] = (struct JE_EventRecType){ .eventtime = 110, .eventtype = 10, .eventdat = SIDE,
	                                        .eventdat2 = 150, .eventdat4 = 11 };
	maxEvent = 3;
	rule = rule && !level_piece_orphaned(2);
	eventRec[1] = eventRec[2];
	eventRec[2] = eventRec[3];
	maxEvent = 2;
	qa_check(rule,
	         "a body's later piece counts as orphaned only when the tile it stacks on is gone: a "
	         "repeat of the same tile, another column, a long gap, a boss link or a different "
	         "piece spawned between them never do");

	eventRec[0].eventtype = eventRec[1].eventtype = 49;
	eventRec[0].eventdat = 191;
	eventRec[1].eventdat = 153;
	bool custom = level_piece_orphaned(1);
	qa_piece_live(75, 0, 150);
	enemy[75].egr[0] = 191;
	custom = custom && !level_piece_orphaned(1);
	enemy[75].egr[0] = 153;
	custom = custom && level_piece_orphaned(1);
	enemyAvail[75] = 1;
	eventRec[0].eventtype = eventRec[1].eventtype = 10;
	eventRec[0].eventdat = BOTTOM;
	eventRec[1].eventdat = TOP;
	qa_check(custom,
	         "a body built from custom-graphic spawns stacks by its graphics and finds its lower "
	         "piece by graphic as well");

	const int skipped = qa_piece_spawn(1);
	endlessMode = false;
	const int vanilla = qa_piece_spawn(1);
	endlessMode = true;
	qa_piece_live(75, BOTTOM, 150);
	const int completed = qa_piece_spawn(1);
	qa_check(skipped == 0 && vanilla > 0 && completed > 0,
	         "in Endless the top of a body whose bottom was shot down before it arrived never "
	         "spawns, while the campaign keeps the vanilla spawn and a living bottom gets its top");

	bool links[256] = { false };
	eventLoc = 2;
	curLoc = 110;
	level_mark_assembling_links(links);
	const bool waiting = links[11];
	memset(links, 0, sizeof(links));
	eventLoc = 3;
	level_mark_assembling_links(links);
	const bool whole = !links[11];
	memset(links, 0, sizeof(links));
	eventLoc = 2;
	enemyAvail[75] = 1;
	level_mark_assembling_links(links);
	qa_check(waiting && whole && !links[11],
	         "a body is assembling from its first piece until its last arrives, and not once its "
	         "pieces are gone");

	qa_piece_live(75, BOTTOM, 150);
	qa_piece_live(76, SIDE, 174);
	player[0].x = 60;
	player[0].y = 160;
	mt_srand(0x5eed5715u);
	bool held = true;
	for (int t = 0; t < 20; ++t)
	{
		endlessGroupHoming();
		held = held && enemy[75].exc == 0 && enemy[75].eyc == 0 && enemy[76].exc == 0 &&
		       enemy[76].eyc == 0 && enemy[75].groupHomed && enemy[76].groupHomed;
	}
	eventLoc = 3;
	bool together = true, moved = false;
	for (int t = 0; t < 20; ++t)
	{
		endlessGroupHoming();
		together = together && enemy[75].exc == enemy[76].exc && enemy[75].eyc == enemy[76].eyc;
		moved = moved || enemy[75].exc != 0 || enemy[75].eyc != 0;
	}
	qa_check(held && together && moved,
	         "the pieces of a body still assembling hold their scripted course under homing, then "
	         "home as one once it is whole");

	memcpy(eventRec, savedEvents, sizeof(eventRec));
	memcpy(enemy, savedEnemy, sizeof(enemy));
	memcpy(enemyAvail, savedAvail, sizeof(savedAvail));
	memcpy(&enemyDat[TOP], savedDat[0], sizeof(enemyDat[0]));
	memcpy(&enemyDat[BOTTOM], savedDat[1], sizeof(enemyDat[0]));
	memcpy(&enemyDat[0], savedDat[2], sizeof(enemyDat[0]));
	memcpy(&enemyDat[SIDE], savedDat[3], sizeof(enemyDat[0]));
	mt_state_restore(savedRng);
	maxEvent = savedMax;
	level_scan_boss_leadins();
	eventLoc = savedLoc;
	curLoc = savedCur;
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	endlessActiveMods = savedMods;
	boss_bar[0] = savedBars[0];
	boss_bar[1] = savedBars[1];
	boss_body_link = savedBody;
	player[0].x = savedX;
	player[0].y = savedY;
	free(savedEvents);
	free(savedEnemy);
	free(savedRng);
}

void qa_test_map_top_pin(void)
{
	JE_byte **const map1 = &megaData1.mainmap[0][0];
	JE_byte **const map3 = &megaData3.mainmap[0][0];
	JE_byte **const top1 = &megaData1.mainmap[1][0];
	JE_byte **const top3 = &megaData3.mainmap[1][0];

	const bool arrives = bg_wrap_target(top1 - 1, top1, top1, 14) == top1 - 1 &&
	                     bg_wrap_target(top3 - 1, top3, top3, 15) == top3 - 1;
	const bool holds = bg_wrap_target(map1 - 1, top1, top1, 14) == top1 - 1 &&
	                   bg_wrap_target(map3 - 1, top3, top3, 15) == top3 - 1;
	JE_byte **const stop = map1 + 5 * 14;
	const bool phase = bg_wrap_target(map1 + 3 * 14 + 10, stop, stop, 14) == map1 + 4 * 14 + 10 &&
	                   bg_wrap_target(map1 + 3 * 14 + 13, stop, stop, 14) == map1 + 4 * 14 + 13;
	qa_check(arrives && holds && phase,
	         "a layer that scrolls into the top of its map holds there in its own column");

	JE_byte **const map2 = &megaData2.mainmap[0][0];
	JE_byte **const loopFrom = map2 + 100, **const loopTo = map2 + 8287;
	qa_check(bg_wrap_target(map2 + 97, loopFrom, loopTo, 14) == loopTo &&
	         bg_wrap_target(map2 + 83, loopFrom, loopTo, 14) == loopTo - 14 &&
	         bg_wrap_target(map2 + 97, loopTo, loopFrom, 14) == loopFrom,
	         "a map loop keeps its whole rows of overshoot and an authored jump lands as authored");
}
