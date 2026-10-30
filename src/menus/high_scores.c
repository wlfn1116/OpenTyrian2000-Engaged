/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "high_scores.h"

#include "audio.h"
#include "config.h"
#include "console_platform.h"
#include "draw.h"
#include "endless.h"
#include "episodes.h"
#include "font.h"
#include "game_state.h"
#include "helptext.h"
#include "item_prices.h"
#include "joystick.h"
#include "keyboard.h"
#include "lds_play.h"
#include "menus.h"
#include "mouse.h"
#include "network.h"
#include "palette.h"
#include "params.h"
#include "picload.h"
#include "player.h"
#include "qa.h"
#include "savegame.h"
#include "sfx.h"
#include "sndmast.h"
#include "sprite.h"
#include "timing.h"
#include "touch_ui.h"
#include "video.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void JE_sortHighScores(void)
{
	T2KHighScoreType tempHiScore;
	for (int table = 0; table < 20; ++table)
	{
		if (t2kHighScores[table][1].score > t2kHighScores[table][0].score)
		{
			memcpy(&tempHiScore,             &t2kHighScores[table][0], sizeof(T2KHighScoreType));
			memcpy(&t2kHighScores[table][0], &t2kHighScores[table][1], sizeof(T2KHighScoreType));
			memcpy(&t2kHighScores[table][1], &tempHiScore,             sizeof(T2KHighScoreType));
		}
		if (t2kHighScores[table][2].score > t2kHighScores[table][1].score)
		{
			memcpy(&tempHiScore,             &t2kHighScores[table][1], sizeof(T2KHighScoreType));
			memcpy(&t2kHighScores[table][1], &t2kHighScores[table][2], sizeof(T2KHighScoreType));
			memcpy(&t2kHighScores[table][2], &tempHiScore,             sizeof(T2KHighScoreType));
		}
		if (t2kHighScores[table][1].score > t2kHighScores[table][0].score)
		{
			memcpy(&tempHiScore,             &t2kHighScores[table][0], sizeof(T2KHighScoreType));
			memcpy(&t2kHighScores[table][0], &t2kHighScores[table][1], sizeof(T2KHighScoreType));
			memcpy(&t2kHighScores[table][1], &tempHiScore,             sizeof(T2KHighScoreType));
		}
	}
}

// The Endless high-score page.

/* Endless record pages drill down from mode to chart rule to difficulty. */
typedef enum
{
	ENDLESS_PAGE_MODES = 0,   // crew size and run mode, at its deepest zone under any rule
	ENDLESS_PAGE_RULES,       // ...that mode's four Base Level rules
	ENDLESS_PAGE_DIFFS,       // ...and that rule's difficulties, the one list an erase happens on
	ENDLESS_PAGE_LEVELS
} EndlessPageLevel;

/* One row per run mode and crew size: the three solo modes, then the three co-op ones. A rule's
 * breakdown has one row per difficulty, plus a leading row for its record on any of them. */
#define ENDLESS_PAGE_MODE_ROWS  (ENDLESS_RUNMODE_COUNT * ENDLESS_PLAYER_TABLES)
#define ENDLESS_PAGE_ROW_TABLE(row) ((row) / ENDLESS_RUNMODE_COUNT)
#define ENDLESS_PAGE_ROW_MODE(row)  ((row) % ENDLESS_RUNMODE_COUNT)
#define ENDLESS_PAGE_DIFF_ROWS  (ENDLESS_DIFFICULTY_COUNT + 1)
#define ENDLESS_PAGE_ROW_ANY    0   // the any-difficulty row; the rest are difficulty slot + 1

/* Everything the page navigates between, so the two halves pass one thing around. Each level keeps
 * its own selection, which is what the level under it is a breakdown of. */
typedef struct
{
	int  level;          // EndlessPageLevel: which list is showing
	int  mode;           // selected mode row, carrying both the crew size and the run mode
	int  variant;        // selected Base Level rule (an EndlessBaseRule)
	int  row;            // selected breakdown row (ENDLESS_PAGE_ROW_ANY, else difficulty slot + 1)
	bool confirmErase;   // an erase is waiting on an answer
	int  confirmChoice;  // which answer the cursor sits on
} EndlessPageState;

/* Page geometry, shared by the draw and input halves below. */
static const int endlessPageXCenter = 160;   // center of the 320px menu field
static const int endlessPageBandY0 = 74, endlessPageBandH = 70;
static const int endlessPageRowDy = 14, endlessPageRowH = 12;
static const int endlessPageDiffDy = 11, endlessPageDiffH = 10;
static const int endlessPageDiffGap = 5;   // sets the any-difficulty row apart from the six under it
static const int endlessPageNoteY = 160;   // first note line, keyed to the band and not to a list
static const int endlessPageConfirmY0 = 100, endlessPageConfirmDy = 14, endlessPageConfirmH = 13;

static const char *const endlessPageConfirmChoice[] = { "No, Keep It", "Yes, Erase It" };

// Size every record list to the widest note so their columns align.
static const char *const endlessPageNote[] =
{
	"C = a custom weapon/ship was used during the run.",
	"Select a mode to break it down by base level.",
	"Select a base level to break it down by difficulty.",
	"Selecting a record erases it, once you confirm.",
};
COMPILE_TIME_ASSERT(endless_page_notes_per_level,
                    COUNTOF(endlessPageNote) == ENDLESS_PAGE_LEVELS + 1);

// Label for a breakdown row: the any-difficulty row, else the difficulty's own name.
static const char *endlessPageDiffName(int row)
{
	if (row == ENDLESS_PAGE_ROW_ANY)
		return "Any Difficulty";
	return difficultyNameB[endlessDifficultyLevel[row - 1]];
}

// The zone and custom mark a breakdown row shows, under the rule the page has narrowed to.
static int endlessPageDiffZone(int variant, int players, EndlessRunMode mode, int row)
{
	return (row == ENDLESS_PAGE_ROW_ANY) ? endlessBestZoneAny(variant, players, mode)
	                                     : endlessBestZoneForDifficulty(variant, players, mode, row - 1);
}

static const char *endlessPageDiffMark(int variant, int players, EndlessRunMode mode, int row)
{
	return (row == ENDLESS_PAGE_ROW_ANY) ? endlessRecordAnyCustomMark(variant, players, mode)
	                                     : endlessRecordDiffCustomMark(variant, players, mode, row - 1);
}

// A mode row's label carries its crew size, since the two sets sit in one list.
static const char *endlessPageModeName(int row)
{
	static char buf[32];
	snprintf(buf, sizeof(buf), "%s %s",
	         (ENDLESS_PAGE_ROW_TABLE(row) == 1) ? "2P" : "1P",
	         endlessRunModeName((EndlessRunMode)ENDLESS_PAGE_ROW_MODE(row)));
	return buf;
}

static int endlessPageRows(int level)
{
	if (level == ENDLESS_PAGE_MODES)
		return ENDLESS_PAGE_MODE_ROWS;
	return (level == ENDLESS_PAGE_RULES) ? ENDLESS_BASE_TABLES : ENDLESS_PAGE_DIFF_ROWS;
}

// One row of whichever list is showing: what it is called, how deep it got, and its custom mark.
typedef struct { const char *label, *mark; int zone; } EndlessPageRow;

static EndlessPageRow endlessPageRowAt(const EndlessPageState *page, int level, int row)
{
	const int table = ENDLESS_PAGE_ROW_TABLE(page->mode);
	const EndlessRunMode mode = (EndlessRunMode)ENDLESS_PAGE_ROW_MODE(page->mode);
	EndlessPageRow out = { "", "", 0 };

	switch (level)
	{
	case ENDLESS_PAGE_MODES:
	{
		const int rowTable = ENDLESS_PAGE_ROW_TABLE(row);
		const EndlessRunMode rowMode = (EndlessRunMode)ENDLESS_PAGE_ROW_MODE(row);
		out.label = endlessPageModeName(row);
		out.zone  = endlessBestZoneAnyRule(rowTable, rowMode);
		out.mark  = endlessRecordAnyRuleCustomMark(rowTable, rowMode);
		break;
	}
	case ENDLESS_PAGE_RULES:
	{
		// In the Base Level row's order, which pairs each rule with its Deck twin.
		const int rule = (int)endlessBaseRuleAtMenuIndex(row);
		out.label = endlessBaseLevelRuleName(rule);
		out.zone  = endlessBestZoneAny(rule, table, mode);
		out.mark  = endlessRecordAnyCustomMark(rule, table, mode);
		break;
	}
	default:
		out.label = endlessPageDiffName(row);
		out.zone  = endlessPageDiffZone(page->variant, table, mode, row);
		out.mark  = endlessPageDiffMark(page->variant, table, mode, row);
		break;
	}
	return out;
}

// Where the cursor sits on the list showing, and where a click puts it. Each level identifies its
// selection in its own terms, so both go through here.
static int endlessPageSelected(const EndlessPageState *page, int level)
{
	if (level == ENDLESS_PAGE_MODES)
		return page->mode;
	if (level == ENDLESS_PAGE_RULES)
		return endlessBaseRuleMenuIndex((EndlessBaseRule)page->variant);
	return page->row;
}

static void endlessPageSelect(EndlessPageState *page, int level, int row)
{
	if (level == ENDLESS_PAGE_MODES)
		page->mode = row;
	else if (level == ENDLESS_PAGE_RULES)
		page->variant = (int)endlessBaseRuleAtMenuIndex(row);
	else
		page->row = row;
}

// How the page names where it has narrowed to, in the words of the rows it came through.
static const char *endlessPageTrail(const EndlessPageState *page, int level)
{
	static char buf[64];
	const char *const table = endlessRecordTableName(ENDLESS_PAGE_ROW_TABLE(page->mode));
	const char *const mode = endlessRunModeName((EndlessRunMode)ENDLESS_PAGE_ROW_MODE(page->mode));

	if (level <= ENDLESS_PAGE_RULES)
		snprintf(buf, sizeof(buf), "%s %s", table, mode);
	else
		snprintf(buf, sizeof(buf), "%s %s, %s", table, mode,
		         endlessBaseLevelRuleName(page->variant));
	return buf;
}

// Center the record block with right-aligned zones and custom marks.
static void endlessPageColumns(int *xLabel, int *xZoneRight)
{
	int blockW = 0;
	for (int i = 0; i < (int)COUNTOF(endlessPageNote); ++i)
		blockW = MAX(blockW, JE_textWidth(endlessPageNote[i], small_font));

	*xLabel = endlessPageXCenter - blockW / 2;
	*xZoneRight = *xLabel + blockW - JE_textWidth(" C", small_font);
}

// One "Label ....... 58 C" row. The mark's own leading space is the gap after the zone.
static void endlessPageDrawRow(int xLabel, int xZoneRight, int y, const char *label,
                               int zoneValue, const char *mark, bool selected)
{
	char text[32], zone[16];
	snprintf(text, sizeof(text), "%s:", label);
	if (zoneValue > 0)
		snprintf(zone, sizeof(zone), "%d", zoneValue);
	else
		SDL_strlcpy(zone, "None", sizeof(zone));

	const int labelShade = selected ? 6 : 0, zoneShade = selected ? 6 : 2;
	JE_textShade(VGAScreen, xLabel, y, text, 15, labelShade, FULL_SHADE);
	JE_textShade(VGAScreen, xZoneRight - JE_textWidth(zone, small_font), y, zone, 15, zoneShade, FULL_SHADE);
	JE_textShade(VGAScreen, xZoneRight, y, mark, 15, zoneShade, FULL_SHADE);
}

// Row baseline on any of the lists. The breakdown's any-difficulty row summarises the six below it,
// so it stands off from them rather than reading as one of them.
static int endlessPageRowY(int level, int row)
{
	if (level == ENDLESS_PAGE_DIFFS)
	{
		return endlessPageBandY0 + endlessPageDiffDy * row
		       + ((row > ENDLESS_PAGE_ROW_ANY) ? endlessPageDiffGap : 0);
	}

	const int span = endlessPageRowDy * (endlessPageRows(level) - 1);
	return endlessPageBandY0 + (endlessPageBandH - span) / 2 + endlessPageRowDy * row;
}

// How tall a row is to the mouse, which follows the pitch its list is drawn at.
static int endlessPageHitH(int level)
{
	return (level == ENDLESS_PAGE_DIFFS) ? endlessPageDiffH : endlessPageRowH;
}

/* Co-op Campaign records use the full legacy 320-pixel field. */
static const int coopPageXLabel = 20, coopPageXRight = 300, coopPageXIndent = 8;

int coopCampaignRecordLineWidthPx(void)
{
	return coopPageXRight - (coopPageXLabel + coopPageXIndent);
}

/* Player names come from the lobby, so the pair can be wider than the row on its own. The terms
 * are what makes two figures comparable, so the names give way to them. */
void coopCampaignRecordLine(char *out, size_t outSize, const CoopCampaignScore *record, int widthPx)
{
	const char *const credit = coopCampaignCreditName(record->credit);
	char terms[48];
	snprintf(terms, sizeof(terms), "  (%s%s%s)",
	         difficultyNameB[MIN(record->difficulty, DIFFICULTY_10)],
	         credit != NULL ? ", " : "", credit != NULL ? credit : "");

	char names[sizeof(record->name)];
	SDL_strlcpy(names, record->name, sizeof(names));

	const int budgetPx = widthPx - JE_textWidth(terms, small_font);
	size_t keep = strlen(names);
	while (keep > 0 && JE_textWidth(names, small_font) > budgetPx)
	{
		--keep;
		SDL_strlcpy(names + keep, "...", sizeof(names) - keep);
	}

	snprintf(out, outSize, "%s%s", names, terms);
}

/* The online co-op Campaign board: the best combined cash each episode has been finished with,
 * and who was flying. Read-only, so it needs no selection or erase machinery. */
static void JE_drawCoopCampaignPage(void)
{
	static const char note[] = "The best combined cash for each episode, online.";

	/* Two tiny-font lines per episode. Both carry a full shade, and the difficulty's parentheses
	 * are the font's tallest glyphs, so each pitch has to clear a descender and its outline. */
	const int yFirst = 75, yPitch = 18, yNameOffset = 8, yNoteGap = 14;

	draw_font_hv_shadow(VGAScreen, endlessPageXCenter, 55, "Two Players, One Campaign",
	                    normal_font, centered, 15, -3, false, 2);

	for (int e = 0; e < COOP_CAMPAIGN_SCORE_EPISODES; ++e)
	{
		const int y = yFirst + yPitch * e;
		char label[40], value[32];
		snprintf(label, sizeof(label), "%s:", episode_name[e + 1]);
		if (coopCampaignScores[e].score > 0)
			snprintf(value, sizeof(value), "%lld", (long long)coopCampaignScores[e].score);
		else
			SDL_strlcpy(value, "None", sizeof(value));

		JE_textShade(VGAScreen, coopPageXLabel, y, label, 15, 0, FULL_SHADE);
		JE_textShade(VGAScreen, coopPageXRight - JE_textWidth(value, small_font), y, value,
		             15, 2, FULL_SHADE);

		/* The names go on their own indented line, so a long pair cannot run into the figure. The
		 * credit rule joins the difficulty: both decide what the figure above them is worth. */
		if (coopCampaignScores[e].score > 0 && coopCampaignScores[e].name[0] != '\0')
		{
			char who[80];
			coopCampaignRecordLine(who, sizeof(who), &coopCampaignScores[e],
			                       coopCampaignRecordLineWidthPx());
			JE_textShade(VGAScreen, coopPageXLabel + coopPageXIndent, y + yNameOffset, who,
			             15, 4, FULL_SHADE);
		}
	}

	const int yNote = yFirst + yPitch * (COOP_CAMPAIGN_SCORE_EPISODES - 1) + yNameOffset + yNoteGap;
	JE_textShade(VGAScreen, endlessPageXCenter - JE_textWidth(note, small_font) / 2, yNote,
	             note, 15, 2, FULL_SHADE);
}

static void JE_drawEndlessRecordPage(const EndlessPageState *page)
{
	int xLabel, xZoneRight;
	endlessPageColumns(&xLabel, &xZoneRight);

	if (page->confirmErase)
	{
		/* The any-difficulty row erases its deepest underlying record. */
		char question[80];
		if (page->row == ENDLESS_PAGE_ROW_ANY)
			SDL_strlcpy(question, "Erase its deepest record?", sizeof(question));
		else
			snprintf(question, sizeof(question), "Erase its record on %s?",
			         endlessPageDiffName(page->row));

		const char *const trail = endlessPageTrail(page, ENDLESS_PAGE_DIFFS);
		draw_font_hv_shadow(VGAScreen, endlessPageXCenter, 55, "Are You Sure?",
		                    normal_font, centered, 15, -3, false, 2);
		JE_textShade(VGAScreen, endlessPageXCenter - JE_textWidth(trail, small_font) / 2, 76,
		             trail, 15, 4, FULL_SHADE);
		JE_textShade(VGAScreen, endlessPageXCenter - JE_textWidth(question, small_font) / 2, 86,
		             question, 15, 2, FULL_SHADE);

		for (int i = 0; i < (int)COUNTOF(endlessPageConfirmChoice); ++i)
		{
			draw_font_hv_shadow(VGAScreen, endlessPageXCenter, endlessPageConfirmY0 + endlessPageConfirmDy * i,
			                    endlessPageConfirmChoice[i], normal_font, centered, 15,
			                    i == page->confirmChoice ? -1 : -4, false, 2);
		}

		static const char note[] = "A record cannot be brought back.";
		JE_textShade(VGAScreen, endlessPageXCenter - JE_textWidth(note, small_font) / 2,
		             endlessPageConfirmY0 + endlessPageConfirmDy * (int)COUNTOF(endlessPageConfirmChoice) + 10,
		             note, 15, 2, FULL_SHADE);
		return;
	}

	// The top list is the whole board, so it names the figures; the ones under it name their slice
	// of it instead.
	draw_font_hv_shadow(VGAScreen, endlessPageXCenter, 55,
	                    (page->level == ENDLESS_PAGE_MODES) ? "Furthest Zone"
	                                                        : endlessPageTrail(page, page->level),
	                    normal_font, centered, 15, -3, false, 2);

	const int rows = endlessPageRows(page->level);
	const int selected = endlessPageSelected(page, page->level);
	for (int i = 0; i < rows; ++i)
	{
		const EndlessPageRow row = endlessPageRowAt(page, page->level, i);
		endlessPageDrawRow(xLabel, xZoneRight, endlessPageRowY(page->level, i),
		                   row.label, row.zone, row.mark, i == selected);
	}

	JE_textShade(VGAScreen, xLabel, endlessPageNoteY, endlessPageNote[0], 15, 2, FULL_SHADE);
	JE_textShade(VGAScreen, xLabel, endlessPageNoteY + 10, endlessPageNote[1 + page->level],
	             15, 2, FULL_SHADE);
}

// Answer a pending erase. Confirming is the only path that touches a record.
static void JE_endlessRecordPageAnswer(EndlessPageState *page, int choice)
{
	if (choice == 1)
	{
		const int table = ENDLESS_PAGE_ROW_TABLE(page->mode);
		const EndlessRunMode mode = (EndlessRunMode)ENDLESS_PAGE_ROW_MODE(page->mode);
		if (page->row == ENDLESS_PAGE_ROW_ANY)
			endlessClearDeepestRecord(page->variant, table, mode);
		else
			endlessClearRecordDifficulty(page->variant, table, mode, page->row - 1);
		JE_playSampleNum(S_ITEM);
	}
	else
	{
		JE_playSampleNum(S_SPRING);
	}
	page->confirmErase = false;
}

// Ask about the selected breakdown row, unless it has no record to lose.
static void JE_endlessRecordPageArm(EndlessPageState *page)
{
	if (endlessPageDiffZone(page->variant, ENDLESS_PAGE_ROW_TABLE(page->mode),
	                        (EndlessRunMode)ENDLESS_PAGE_ROW_MODE(page->mode), page->row) > 0)
	{
		page->confirmErase = true;
		page->confirmChoice = 0;   // always opens on "No, Keep It"
		JE_playSampleNum(S_SELECT);
	}
	else
	{
		JE_playSampleNum(S_SPRING);   // nothing to erase
	}
}

// Step in from the selected row, or arm an erase once there is nothing left to narrow.
static void JE_endlessRecordPageEnter(EndlessPageState *page)
{
	if (page->level == ENDLESS_PAGE_DIFFS)
	{
		JE_endlessRecordPageArm(page);
		return;
	}

	// Open the level below on its first row, so stepping into a different mode or rule starts from
	// that one's own summary.
	++page->level;
	endlessPageSelect(page, page->level, 0);
	JE_playSampleNum(S_SELECT);
}

// Endless-page input. Returns true when it consumed the tick, which leaves the shared paging and
// exit handling untouched. A pending erase consumes everything, so nothing else can act under it.
static bool JE_endlessRecordPageInput(EndlessPageState *page)
{
	const int rows = endlessPageRows(page->level);
	const int rowH = endlessPageHitH(page->level);

	if (newmouse)
	{
		if (page->confirmErase)
		{
			if (lastmouse_but == SDL_BUTTON_LEFT)
			{
				for (int i = 0; i < (int)COUNTOF(endlessPageConfirmChoice); ++i)
				{
					const int y = endlessPageConfirmY0 + endlessPageConfirmDy * i;
					const int w = JE_textWidth(endlessPageConfirmChoice[i], normal_font);
					if (mouse_y >= y && mouse_y < y + endlessPageConfirmH
					    && mouse_x >= endlessPageXCenter - w / 2 && mouse_x < endlessPageXCenter + w / 2)
					{
						JE_endlessRecordPageAnswer(page, i);
						return true;
					}
				}
			}
			else if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);
				page->confirmErase = false;
			}
			return true;   // a pending answer never falls through to paging or exit
		}

		if (lastmouse_but == SDL_BUTTON_RIGHT && page->level > ENDLESS_PAGE_MODES)
		{
			JE_playSampleNum(S_SPRING);   // back out one level; only the mode list leaves the screen
			--page->level;
			return true;
		}

		if (lastmouse_but == SDL_BUTTON_LEFT)
		{
			int xLabel, xZoneRight;
			endlessPageColumns(&xLabel, &xZoneRight);

			for (int i = 0; i < rows; ++i)
			{
				const int y = endlessPageRowY(page->level, i);
				// The whole label-to-record span is clickable, gaps included.
				if (mouse_y >= y && mouse_y < y + rowH
				    && mouse_x >= xLabel && mouse_x < endlessPageXCenter + (endlessPageXCenter - xLabel))
				{
					endlessPageSelect(page, page->level, i);
					JE_endlessRecordPageEnter(page);
					return true;
				}
			}
		}
		return false;
	}

	if (!newkey)
		return false;

	switch (lastkey_scan)
	{
	case SDL_SCANCODE_UP:
	case SDL_SCANCODE_DOWN:
	{
		const int step = (lastkey_scan == SDL_SCANCODE_UP) ? -1 : 1;
		if (page->confirmErase)
		{
			const int n = (int)COUNTOF(endlessPageConfirmChoice);
			page->confirmChoice = (page->confirmChoice + n + step) % n;
		}
		else
		{
			endlessPageSelect(page, page->level,
			                  (endlessPageSelected(page, page->level) + rows + step) % rows);
		}
		JE_playSampleNum(S_CURSOR);
		return true;
	}
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER:
	{
		if (page->confirmErase)
			JE_endlessRecordPageAnswer(page, page->confirmChoice);
		else
			JE_endlessRecordPageEnter(page);
		return true;
	}
	default:
		// Esc unwinds one level at a time; a pending answer or a narrowed list swallows the rest.
		if (page->confirmErase)
		{
			if (lastkey_scan == SDL_SCANCODE_ESCAPE)
			{
				JE_playSampleNum(S_SPRING);
				page->confirmErase = false;
			}
			return true;
		}
		if (page->level > ENDLESS_PAGE_MODES)
		{
			if (lastkey_scan == SDL_SCANCODE_ESCAPE)
			{
				JE_playSampleNum(S_SPRING);
				--page->level;
			}
			return true;
		}
		return false;
	}
}

static void JE_drawHighScoreRows(int board, int y0)
{
	char buffer[64];

	for (Uint8 i = 0; i < 3; ++i)
	{
		const int y = y0 + 10 * i;

		if (t2kHighScores[board][i].difficulty > 9)
			t2kHighScores[board][i].difficulty = 0;

		const int rank = t2kHighScores[board][i].difficulty;
		const Sint64 score = t2kHighScores[board][i].score;
		const char *name = t2kHighScores[board][i].playerName;

		snprintf(buffer, sizeof buffer, "~#%d:~  %lld", i + 1, (long long)score);
		JE_textShade(VGAScreen, 20, y, buffer, 15, 0, FULL_SHADE);
		JE_textShade(VGAScreen, 110, y, name, 15, 2, FULL_SHADE);
		JE_textShade(VGAScreen, 250, y, difficultyNameB[rank], 15, rank + (rank == 0 ? 0 : -1), FULL_SHADE);
	}
}

void JE_highScoreScreen(void)
{
	set_menu_centered(true);

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer and arrow sprites

	// The press that opened the boards would page straight through them.
	wait_noinput_consume(true, true, true);

	bool restart = true;

	size_t episodeIndex = 0;
	/* Top-level score pages: episodes, timed battles, co-op Campaign, and Endless. */
	const size_t endlessPage = 9;
	const size_t episodeCount = endlessPage + 1;
	const size_t coopPage = endlessPage - 1;

	// Endless page state: which list is showing, its selection at each level, and a pending erase.
	EndlessPageState endlessPageState = {
		ENDLESS_PAGE_MODES, 0, ENDLESS_BASE_VARIED, ENDLESS_PAGE_ROW_ANY, false, 0
	};

	const int xCenter = 160; // center of 320px menu field
	const int yMenuHeader = 3;
	const int yEpisodeHeader = 30;
	const int xLeftControl = 83;
	const int xRightControl = 213;
	const int wControl = 24;
	const int yControls = vga_height - 21;

	char buffer[64];
	int boardOnePlayer, boardTwoPlayer;

	for (; ; )
	{
		if (restart)
		{
			JE_loadPic(VGAScreen2, 2, false);
			fill_rectangle_wh(VGAScreen2, 0, vga_height - 8, vga_width, 8, 0);

			// Draw header.
			draw_font_hv_shadow(VGAScreen2, xCenter, yMenuHeader, miscText[50], large_font, centered, 15, -3, false, 2);
		}

		// Restore background and header.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		const bool onEndlessPage = episodeIndex == endlessPage;
		if (onEndlessPage)
		{
			SDL_strlcpy(buffer, "Endless", sizeof(buffer));

			// No score board: the Endless page lists the per-mode zone records instead.
			boardOnePlayer = -1;
			boardTwoPlayer = -1;
		}
		else if (episodeIndex == coopPage)
		{
			SDL_strlcpy(buffer, "2 Player Campaign", sizeof(buffer));

			// Its own board, drawn below: one best run per episode rather than a top three.
			boardOnePlayer = -1;
			boardTwoPlayer = -1;
		}
		else if (episodeIndex < 5)
		{
			snprintf(buffer, sizeof(buffer), "%s", episode_name[episodeIndex + 1]);

			// Regular episode boards
			boardOnePlayer = 10 + (episodeIndex * 2);
			boardTwoPlayer = 11 + (episodeIndex * 2);
		}
		else
		{
			snprintf(buffer, sizeof(buffer), "%s %s", timed_battle_name[0], timed_battle_name[episodeIndex - 4]);

			// Timed Battle boards
			boardOnePlayer = episodeIndex - 5;
			boardTwoPlayer = -1;
		}

		// Draw episode header.
		draw_font_hv_shadow(VGAScreen, xCenter, yEpisodeHeader, buffer, normal_font, centered, 15, -3, false, 2);

		if (onEndlessPage)
			JE_drawEndlessRecordPage(&endlessPageState);
		else if (episodeIndex == coopPage)
			JE_drawCoopCampaignPage();

		// Draw 1-player scores.
		if (boardOnePlayer >= 0)
		{
			draw_font_hv_shadow(VGAScreen, xCenter, 55, miscText[46], normal_font, centered, 15, -3, false, 2);
			JE_drawHighScoreRows(boardOnePlayer, 75);
		}

		// Draw 2-player scores.
		if (boardTwoPlayer >= 0)
		{
			draw_font_hv_shadow(VGAScreen, xCenter, 120, miscText[47], normal_font, centered, 15, -3, false, 2);
			JE_drawHighScoreRows(boardTwoPlayer, 135);
		}

		// Draw paging controls. A breakdown or a pending erase owns the page, so paging away is
		// not offered until the player has backed out of it.
		const bool endlessBusy = onEndlessPage
		                      && (endlessPageState.level > ENDLESS_PAGE_MODES
		                          || endlessPageState.confirmErase);
		const bool leftControlVisible = episodeIndex > 0 && !endlessBusy;
		const bool rightControlVisible = episodeIndex < episodeCount - 1 && !endlessBusy;

		if (leftControlVisible)
			blit_sprite2x2(VGAScreen, xLeftControl, yControls, shopSpriteSheet, 279);

		if (rightControlVisible)
			blit_sprite2x2(VGAScreen, xRightControl, yControls, shopSpriteSheet, 281);

		// The paging hint is wrong while the page owns the input: left and right do nothing then.
		if (!endlessBusy)
		{
			helpBoxColor = 15;
			JE_helpBox(VGAScreen, 103, vga_height - 18, miscText[56], 25);
		}

		if (restart)
		{
			mouseCursor = MOUSE_POINTER_NORMAL;

			fade_palette(colors, 10, 0, 255);

			restart = false;
		}

		do
		{
			service_SDL_events(true);

			JE_mouseStart();
			JE_showVGA();
			JE_mouseReplace();

			// Present at display rate for a smooth cursor; vsync-on paces via showVGA.
			if (!output_vsync)
				limit_render_fps();

			push_joysticks_as_keyboard();
			service_SDL_events(false);
		} while (!(newkey || newmouse));

		// Handle interaction.

		if (onEndlessPage && JE_endlessRecordPageInput(&endlessPageState))
			continue;

		bool leftAction = false;
		bool rightAction = false;
		bool done = false;

		if (newmouse)
		{
			switch (lastmouse_but)
			{
			case SDL_BUTTON_LEFT:
			{
				if (leftControlVisible &&
				    mouse_y >= yControls &&
				    mouse_x >= xLeftControl &&
				    mouse_x < xLeftControl + wControl)
				{
					JE_playSampleNum(S_CURSOR);

					leftAction = true;
				}
				else if (rightControlVisible &&
				         mouse_y >= yControls &&
				         mouse_x >= xRightControl &&
				         mouse_x < xRightControl + wControl)
				{
					JE_playSampleNum(S_CURSOR);

					rightAction = true;
				}
				break;
			}
			case SDL_BUTTON_RIGHT:
			{
				JE_playSampleNum(S_SPRING);

				done = true;
				break;
			}
			default:
				break;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_LEFT:
			{
				JE_playSampleNum(S_CURSOR);

				leftAction = true;
				break;
			}
			case SDL_SCANCODE_RIGHT:
			{
				JE_playSampleNum(S_CURSOR);

				rightAction = true;
				break;
			}
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_ESCAPE:
			{
				JE_playSampleNum(S_SPRING);

				done = true;
				break;
			}
			default:
				break;
			}
		}

		if (leftAction)
		{
			episodeIndex = episodeIndex == 0
				? episodeCount - 1
				: episodeIndex - 1;
		}
		else if (rightAction)
		{
			episodeIndex = episodeIndex == episodeCount - 1
				? 0
				: episodeIndex + 1;
		}

		if (done)
		{
			fade_black(15);

			return;
		}
	}
}

void JE_highScoreCheck(void)
{
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprite

	Sint32 temp_score;

	for (int temp_p = 0; temp_p < (twoPlayerMode ? 2 : 1); ++temp_p)
	{
		JE_sortHighScores();

		int p = temp_p;
		int table = 10 + (initial_episode_num - 1) * 2;

		if (timedBattleMode)
		{
			// timed battle score is just money
			temp_score = player[0].cash;
			table = timeBattleSelection - 1;
		}
		else if (twoPlayerMode)
		{
			// ask for the highest scorer first
			if (player[0].cash < player[1].cash)
				p = (temp_p == 0) ? 1 : 0;

			temp_score = (p == 0) ? player[0].cash : player[1].cash;
			++table;
		}
		else
		{
			// single player highscore includes cost of upgrades
			temp_score = JE_totalScore(&player[0]);
		}

		int slot;
		for (slot = 0; slot < 3; ++slot)
		{
			if (temp_score > t2kHighScores[table][slot].score)
				break;
		}

		// Check for a high score.
		if (slot < 3)
		{
			// shift down old scores
			for (int i = 2; i > slot; --i)
				memcpy(&t2kHighScores[table][i], &t2kHighScores[table][i - 1], sizeof(T2KHighScoreType));

			wait_noinput(false, true, false);

			JE_clr256(VGAScreen);
			JE_showVGA();
			memcpy(colors, palettes[0], sizeof(colors));

			if (!timedBattleMode) // Doesn't play this music
				play_song(33);

			// Not part of the above condition
			{
				/* Enter Thy name */

				JE_byte flash = 8 * 16 + 10;
				JE_boolean fadein = true;
				JE_boolean quit = false, cancel = false;
				char stemp[30], tempstr[30];
				char buffer[256];

				// Use absolute pointer input for the on-screen OK and Cancel buttons.
				const bool hs_was_relative = mouseGetRelative();
				mouseSetRelative(false);

				strcpy(stemp, "                             ");
				temp = 0;

#ifdef PLATFORM_HANDHELD
				// No physical keyboard here: get the name from the software keyboard
				// and fill the field directly (see JE_operation for why not injected as an event).
				{
					char kb[29];
					kb[0] = '\0';
					if (console_swkbd(kb, sizeof(kb), 28, NULL, "Enter your name", false))
					{
						for (const char *c = kb; *c != '\0' && temp < 28; ++c)
						{
							const char u = (unsigned char)*c <= 127U ? (char)toupper((unsigned char)*c) : 0;
							if (u == ' ' || font_ascii[(unsigned char)u] != -1)
								stemp[temp++] = u;
						}
					}
				}
#endif

				// As astoundingly ugly as this makes the shade below look, this is in fact what Tyrian 2000 does.
				if (timedBattleMode)
					JE_loadPic(VGAScreen, 13, false);

				JE_barShade(VGAScreen, 65, 55, 255, 155);

				do
				{
					service_SDL_events(true);

					JE_dString(VGAScreen, JE_fontCenter(miscText[51], FONT_SHAPES), 3, miscText[51], FONT_SHAPES);

					temp3 = twoPlayerMode ? 58 + p : 53;

					JE_dString(VGAScreen, JE_fontCenter(miscText[temp3-1], SMALL_FONT_SHAPES), 30, miscText[temp3-1], SMALL_FONT_SHAPES);

					blit_sprite(VGAScreenSeg, 50, 50, OPTION_SHAPES, 35);  // message box

					if (twoPlayerMode)
					{
						sprintf(buffer, "%s %s", miscText[48 + p], miscText[53]);
						JE_textShade(VGAScreen, 60, 55, buffer, 11, 4, FULL_SHADE);
					}
					else
					{
						JE_textShade(VGAScreen, 60, 55, miscText[53], 11, 4, FULL_SHADE);
					}

					sprintf(buffer, "%s %lld", miscText[37], (long long)temp_score);
					JE_textShade(VGAScreen, 70, 70, buffer, 11, 4, FULL_SHADE);

					do
					{
						flash = (flash == 8 * 16 + 10) ? 8 * 16 + 2 : 8 * 16 + 10;
						temp3 = (temp3 == 6) ? 2 : 6;

						strncpy(tempstr, stemp, temp);
						tempstr[temp] = '\0';
						JE_outText(VGAScreen, 65, 89, tempstr, 8, 3);
						tempW = 65 + JE_textWidth(tempstr, TINY_FONT);
						JE_barShade(VGAScreen, tempW + 2, 90, tempW + 6, 95);
						fill_rectangle_xy(VGAScreen, tempW + 1, 89, tempW + 5, 94, flash);

						for (int i = 0; i < 14; i++)
						{
							setDelay(1);

							JE_mouseStart();
							JE_showVGA();
							if (fadein)
							{
								fade_palette(colors, 15, 0, 255);
								fadein = false;
							}
							JE_mouseReplace();

							push_joysticks_as_keyboard();
							service_wait_delay();

							if (newkey || newmouse)
								break;
						}

					} while (!newkey && !newmouse && !new_text);

					if (!playing)
						play_song(31);

					if (mouseButton > 0)
					{
						if (mouseX > 56 && mouseX < 142 && mouseY > 123 && mouseY < 149)
						{
							quit = true;
						}
						else if (mouseX > 151 && mouseX < 237 && mouseY > 123 && mouseY < 149)
						{
							quit = true;
							cancel = true;
						}
					}
					else if (new_text)
					{
						for (size_t ti = 0U; last_text[ti] != '\0'; ++ti)
						{
							const char c = (unsigned char)last_text[ti] <= 127U ? toupper(last_text[ti]) : 0;
							if ((c == ' ' || font_ascii[(unsigned char)c] != -1) &&
							    temp < 28)
							{
								stemp[temp] = c;
								temp += 1;
							}
						}
					}
					else if (newkey)
					{
						switch (lastkey_scan)
						{
							case SDL_SCANCODE_BACKSPACE:
							case SDL_SCANCODE_DELETE:
								if (temp)
								{
									temp--;
									stemp[temp] = ' ';
								}
								break;
							case SDL_SCANCODE_ESCAPE:
								quit = true;
								cancel = true;
								break;
							case SDL_SCANCODE_RETURN:
								quit = true;
								break;
							default:
								break;
						}
					}
				} while (!quit);

				mouseSetRelative(hs_was_relative);

				// Timed Battle mode doesn't allow cancelling, so we ignore it
				if (!cancel || timedBattleMode)
				{
					t2kHighScores[table][slot].score = temp_score;
					strcpy(t2kHighScores[table][slot].playerName, stemp);
					t2kHighScores[table][slot].difficulty = difficultyLevel;
				}

				fade_black(15);
				JE_loadPic(VGAScreen, 2, false);

				JE_dString(VGAScreen, JE_fontCenter(miscText[50], FONT_SHAPES), 10, miscText[50], FONT_SHAPES);

				if (timedBattleMode)
					JE_dString(VGAScreen, JE_fontCenter(timed_battle_name[timeBattleSelection], SMALL_FONT_SHAPES), 35, timed_battle_name[timeBattleSelection], SMALL_FONT_SHAPES);
				else
					JE_dString(VGAScreen, JE_fontCenter(episode_name[episodeNum], SMALL_FONT_SHAPES), 35, episode_name[episodeNum], SMALL_FONT_SHAPES);

				for (int i = 0; i < 3; ++i)
				{
					if (i != slot)
					{
						sprintf(buffer, "~#%d:~  %lld", i+1, (long long)t2kHighScores[table][i].score);
						JE_textShade(VGAScreen,  20, (i * 12) + 65, buffer, 15, 0, FULL_SHADE);
						JE_textShade(VGAScreen, 150, (i * 12) + 65, t2kHighScores[table][i].playerName, 15, 2, FULL_SHADE);
					}
				}

				JE_showVGA();

				fade_palette(colors, 15, 0, 255);

				// A surviving press would zero frameCountMax in the glow and skip the wait.
				wait_noinput_consume(true, true, true);

				sprintf(buffer, "~#%d:~  %lld", slot+1, (long long)t2kHighScores[table][slot].score);

				frameCountMax = 6;
				textGlowFont = TINY_FONT;

				textGlowBrightness = 10;
				JE_outTextGlow(VGAScreenSeg,  20, (slot * 12) + 65, buffer);
				textGlowBrightness = 10;
				JE_outTextGlow(VGAScreenSeg, 150, (slot * 12) + 65, t2kHighScores[table][slot].playerName);
				textGlowBrightness = 10;
				JE_outTextGlow(VGAScreenSeg, JE_fontCenter(miscText[4], TINY_FONT), vga_height, miscText[4]);

				JE_showVGA();

				if (frameCountMax != 0)
					wait_input(true, true, true);

				fade_black(15);
			}

		}
	}
}

/* Online Timed Battle shows both settled scores without opening the blocking
 * solo name-entry dialog. Names already came from the handshake. */
void JE_timedBattleResult(void)
{
	const uint me = thisPlayerNum >= 2 ? 1u : 0u;
	const char *name[2];
	name[me] = network_player_name[0] ? network_player_name : "You";
	name[1 - me] = network_opponent_name[0] ? network_opponent_name : "Opponent";

	char buffer[128];

	// Pillarbox the legacy 320-pixel results layout on every exit path.
	const bool prevCentered = (video_get_menu_x_offset() != 0);
	set_menu_centered(true);

	fade_black(15);
	// Reuse the opening card's backdrop and mode title.
	JE_loadPic(VGAScreen, 2, false);
	draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, 20, "Timed Battle", large_font,
	                    centered, 15, -3, false, 2);
	draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, 48, timed_battle_name[timeBattleSelection],
	                    normal_font, centered, 15, -3, false, 2);

	const uint winner = player[0].cash == player[1].cash ? 2u
	                  : player[0].cash > player[1].cash ? 0u : 1u;

	for (uint i = 0; i < 2; ++i)
	{
		// Name on the left of a fixed column and total right-aligned on it, so a long name cannot
		// push a total off the panel and neither row's figures wander as the numbers change.
		snprintf(buffer, sizeof(buffer), "%lld", (long long)player[i].cash);
		// The menu range for this bank and font: the top of it is white, which this backdrop is
		// far too dark for. The winner takes the step a selected menu row takes, and no more.
		const int value = (i == winner) ? -2 : -4;
		draw_font_hv_shadow(VGAScreen, 60, 84 + 18 * (int)i, name[i], normal_font,
		                    left_aligned, 15, value, false, 2);
		draw_font_hv_shadow(VGAScreen, 260, 84 + 18 * (int)i, buffer, normal_font,
		                    right_aligned, 15, value, false, 2);
	}

	if (winner < 2)
		snprintf(buffer, sizeof(buffer), "%s wins the race.", name[winner]);
	else
		snprintf(buffer, sizeof(buffer), "A draw.");
	draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, 130, buffer, normal_font, centered, 15, -3, false, 2);

	draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, 172, "Press a key", small_font,
	                    centered, 15, 2, false, 1);

	JE_showVGA();
	// Offer Select before the card fades in and keep it live through the wait.
	touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);
	fade_palette(colors, 15, 0, 255);

	/* Release the input that reached this card. The rendezvous then accepts a fresh local press or
	 * the peer's dismissal, so either player can continue without closing the UDP socket early. */
	if (!constantPlay && qa_net_gameplay_ticks == 0)
		wait_noinput(true, true, true);

	/* Headless scenario 20 makes the guest the sole initiator. The host must leave through the
	 * peer-dismiss path that interactive online play uses. */
	const bool auto_dismiss = constantPlay ||
	                          (qa_net_gameplay_ticks > 0 && thisPlayerNum != networkHostPlayerNum);
	network_end_screen_rendezvous(auto_dismiss);

	fade_black(15);
	set_menu_centered(prevCentered);
}
