#include "qa.h"

#include "episodes.h"
#include "file.h"
#include "game_state.h"
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
	bool read_all = true;
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
		}
		fclose(f);
	}

	memcpy(eventRec, saved, sizeof(eventRec));
	free(saved);
	maxEvent = saved_events;
	episodeNum = saved_episode;
	lvlFileNum = saved_lvl_file;

	qa_check(read_all && levels > 0, "every shipped level's event script reads through the loader");
	qa_check(!episodeAvail[4 - 1] || (nubs > 0 && attached == nubs),
	         "SURFACE's arch nubs spawn beside their arches, as in Tyrian 2.1");
	qa_check(scattered == 0, "outside the authored designs, no spawn group places a piece over "
	         "100px from its median height");
	printf("# level events: %u levels, %u SURFACE arch nubs, %u scattered spawns\n",
	       levels, nubs, scattered);
}
