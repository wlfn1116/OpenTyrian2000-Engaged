#include "qa.h"

#include "background.h"
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

// Count the loaded SURFACE nubs, returning how many sit where the load-time correction puts them.
static unsigned int qa_attached_surface_nubs(unsigned int *nubs)
{
	unsigned int attached = 0;
	for (JE_word i = 0; i < maxEvent; ++i)
	{
		const struct JE_EventRecType *const nub = &eventRec[i];
		if (nub->eventtype != 7 || nub->eventdat != 212)
			continue;

		++*nubs;
		for (JE_word j = 0; j < maxEvent; ++j)
		{
			const struct JE_EventRecType *const arch = &eventRec[j];
			if (arch->eventtype == 7 && arch->eventdat == 213 &&
			    arch->eventtime == nub->eventtime &&
			    arch->eventdat2 == nub->eventdat2 + 18 && arch->eventdat5 == nub->eventdat5 - 6)
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

	unsigned int levels = 0, nubs = 0, attached = 0, scattered = 0;
	bool read_all = true, gryphon = false, harvest = false, underdeli = false;
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
			if (ep == 4 && lvlFileNum == 4)
				attached += qa_attached_surface_nubs(&nubs);
			if (ep == 2 && lvlFileNum == 6)
				gryphon = qa_gryphon_bar_follows_head();
			if (ep == 4 && lvlFileNum == 1)
				harvest = qa_leadin_holds(6200, 10) && !qa_leadin_holds(5000, 10) &&
				          !qa_leadin_holds(6400, 10);
			if (ep == 4 && lvlFileNum == 7)
				underdeli = qa_leadin_holds(4700, 18) && qa_leadin_holds(5500, 17) &&
				            !qa_leadin_holds(4500, 17);
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
	qa_check(scattered == 0, "outside the authored designs, no spawn group places a piece over "
	         "100px from its median height");
	qa_check(!episodeAvail[2 - 1] || gryphon,
	         "GRYPHON keeps one bar: the body's in the campaign, the head's under scaled boss "
	         "HP, with the body still on boss HP");
	printf("# level events: %u levels, %u SURFACE arch nubs, %u scattered spawns\n",
	       levels, nubs, scattered);
}
