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
#include "ship_editor.h"

#include "config.h"
#include "custom_weapon.h"
#include "debug_menu.h"
#include "draw.h"
#include "editor_ui.h"
#include "episodes.h"
#include "extra_ships.h"
#include "file.h"
#include "font.h"
#include "game_loop.h"
#include "game_menu.h"
#include "game_state.h"
#include "hud.h"
#include "image_png.h"
#include "joystick.h"
#include "keyboard.h"
#include "lvlmast.h"
#include "mouse.h"
#include "opentyr.h"
#include "palette.h"
#include "picload.h"
#include "sfx.h"
#include "sndmast.h"
#include "sprite.h"
#include "sprite_picker.h"
#include "timing.h"
#include "touch_ui.h"
#include "video.h"

#include "SDL.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool seLegacyUserShapesAvailable(void)
{
	const char *location = NULL;
	FILE *const f = extraShipsOpenLegacyUserShapes(&location);
	if (f == NULL)
		return false;
	fclose(f);
	return true;
}

enum
{
	SE_ROW_SLOT,
	SE_ROW_GRAPHIC,
	SE_ROW_FRONT,
	SE_ROW_REAR,
	SE_ROW_SPECIAL,
	SE_ROW_LEFT,
	SE_ROW_RIGHT,
	SE_ROW_GENERATOR,
	SE_ROW_SHIELD,
	SE_ROW_ARMOR,
	SE_ROW_COUNT,
	SE_RESTORE_DEFAULTS = SE_ROW_COUNT,
	SE_TOGGLE_STARS,
	SE_ACT_SPRITES,
	SE_ACT_IMPORT,
	SE_ACT_REVERT,
	SE_ACT_DONE,
	SE_NAV_COUNT,
	SE_ACT_COUNT = SE_NAV_COUNT - SE_ACT_SPRITES,
};

static const struct { const char *label, *help; } seRows[SE_ROW_COUNT] = {
	{ "Ship",           "Choose one of ten custom ships. In flight, hold Tab + its number." },
	{ "Graphic",        "Hull artwork or a custom bank. Enter or click opens the grid." },
	{ "Front Weapon",   "The main gun this ship flies with." },
	{ "Rear Weapon",    "The rear gun, or None." },
	{ "Special",        "The special weapon, or None." },
	{ "Left Sidekick",  "The companion in the left bay, or None." },
	{ "Right Sidekick", "The companion in the right bay, or None." },
	{ "Generator",      "Recharges the shield and feeds the guns." },
	{ "Shield",         "The shield model fitted to this ship." },
	{ "Armor",          "Hull strength, 1 to 30." },
};

static const char *const seDefaultsHelp =
	"Restore all ten ships and sprite banks from the stock file.";
static const char *const seDefaultsConfirmHelp =
	"Choose Defaults again to confirm the stock restore.";
static const char *const seStarsHelp = "Show the drifting stars behind the preview.";
static const char *const seRevertConfirmHelp =
	"Choose Revert again to discard the editor's changes.";

static const struct { const char *label, *help; } seActs[SE_ACT_COUNT] = {
	{ "Sprites", "Draw or copy artwork for your eight custom banks." },
	{ "Import",  "Load all ten ships and their artwork from User.shp." },
	{ "Revert",  "Discard changes made since you opened the editor." },
	{ "Done",    "Save your custom ships and leave." },
};

static bool seConfirmAction(bool *armed)
{
	if (*armed)
	{
		*armed = false;
		return true;
	}

	*armed = true;
	return false;
}

bool JE_shipEditorConfirmationSelfTest(void)
{
	bool armed = false;
	return !seConfirmAction(&armed) && armed && seConfirmAction(&armed) && !armed;
}

// Bytes 0..6 follow the row order; armor and shield stay at file bytes 7 and 8.
static int seFieldByte(int row)
{
	return (row == SE_ROW_ARMOR) ? EXTRA_SHIP_BYTE_ARMOR
	     : (row == SE_ROW_SHIELD) ? EXTRA_SHIP_BYTE_SHIELD : row - SE_ROW_GRAPHIC;
}

static JE_byte *seField(int slot, int row)
{
	return &extraShips[(slot - 1) * EXTRA_SHIP_RECORD + seFieldByte(row)];
}

static bool seCustomPortAvailable(void)
{
	return customWeaponPort > 0;
}

static bool seValueOk(int row, int v)
{
	return extraShipFieldOk(seFieldByte(row), v);
}

// Preserve a stored custom weapon when the editor cannot offer it for selection.
static bool seValueOffered(int row, int v)
{
	if (v == EXTRA_SHIP_CUSTOM_PORT && !seCustomPortAvailable())
		return false;
	return seValueOk(row, v);
}

// Keep stored IDs stable while placing the two wide hulls together in the picker.
static int seGraphicOrder(int *order)
{
	const int max = extraShipGraphicMax();
	int count = 0, nort = 0;
	for (int graphic = 1; graphic <= max; ++graphic)
		if (extraShipGraphicIsNort(graphic))
		{
			nort = graphic;
			break;
		}

	for (int graphic = 1; graphic <= MIN(6, max); ++graphic)
		order[count++] = graphic;
	if (nort != 0)
		order[count++] = nort;
	for (int graphic = 7; graphic <= max; ++graphic)
		if (graphic != nort)
			order[count++] = graphic;
	return count;
}

static int seNextGraphic(int current, int dir)
{
	int order[256];
	return spritePickerCycle(order, seGraphicOrder(order), current, dir);
}

static void seStepField(int slot, int row, int dir)
{
	JE_byte *const p = seField(slot, row);
	if (row == SE_ROW_GRAPHIC)
	{
		*p = (JE_byte)seNextGraphic(*p, dir);
		return;
	}

	int v = *p;
	for (int guard = 0; guard < 256; ++guard)
	{
		v += dir;
		if (v < 0)
			v = 255;
		else if (v > 255)
			v = 0;
		if (seValueOffered(row, v))
		{
			*p = (JE_byte)v;
			return;
		}
	}
}

bool JE_shipEditorGraphicCycleSelfTest(void)
{
	const int nort = seNextGraphic(6, 1);
	return extraShipGraphicIsNort(nort) && seNextGraphic(nort, 1) == 7 &&
	       seNextGraphic(7, -1) == nort && seNextGraphic(nort, -1) == 6;
}

static const char *seValueText(int row, int slot, char *buf, size_t bufSize)
{
	const int v = (row == SE_ROW_SLOT) ? slot : *seField(slot, row);
	switch (row)
	{
	case SE_ROW_SLOT:
		snprintf(buf, bufSize, "%d of 10", v);
		return buf;
	case SE_ROW_GRAPHIC:
		return extraShipGraphicLabel(v, buf, bufSize);
	case SE_ROW_FRONT:
	case SE_ROW_REAR:
		if (v == EXTRA_SHIP_CUSTOM_PORT)
		{
			snprintf(buf, bufSize, "%s", customWeaponName[0] != '\0' ? customWeaponName : "Custom Weapon");
			return buf;
		}
		return (v == 0 || v > PORT_NUM) ? "None"
		                                : JE_trimItemName(weaponPort[v].name, buf, bufSize);
	case SE_ROW_SPECIAL:
		return (v == 0 || v > SPECIAL_NUM) ? "None"
		                                   : JE_trimItemName(special[v].name, buf, bufSize);
	case SE_ROW_LEFT:
	case SE_ROW_RIGHT:
		return (v == 0 || v > OPTION_NUM) ? "None" : JE_trimItemName(options[v].name, buf, bufSize);
	case SE_ROW_GENERATOR:
		return (v > POWER_NUM) ? "None" : JE_trimItemName(powerSys[v].name, buf, bufSize);
	case SE_ROW_ARMOR:
		snprintf(buf, bufSize, "%d", v);
		return buf;
	case SE_ROW_SHIELD:
		return (v > SHIELD_NUM) ? "None" : JE_trimItemName(shields[v].name, buf, bufSize);
	}
	return "";
}

// Raw graphics 0 and 1 are the two-piece Dragonwing and Nort Ship sentinels.
static void seDrawHull(int cx, int y, Sprite2_array *sheet, JE_word gr, int banking)
{
	const int pose = banking * 2;
	if (gr == 0)
	{
		blit_sprite2x2(VGAScreen, cx - 2 * SHOP_WIDE_HULL_HALF, y, *sheet, 13 + pose);
		blit_sprite2x2(VGAScreen, cx, y, *sheet, 51 + pose);
	}
	else if (gr == 1)
	{
		blit_sprite2x2(VGAScreen, cx - 2 * SHOP_WIDE_HULL_HALF, y, *sheet, 220);
		blit_sprite2x2(VGAScreen, cx, y, *sheet, 222);
		unsigned int trim = 0;
		int trimX = 0;
		switch (banking)
		{
		case -2: trim = 59; trimX = cx + SHOP_WIDE_HULL_HALF; break;
		case -1: trim = 58; trimX = cx + SHOP_WIDE_HULL_HALF; break;
		case  1: trim = 39; trimX = cx - 2 * SHOP_WIDE_HULL_HALF; break;
		case  2: trim = 40; trimX = cx - 2 * SHOP_WIDE_HULL_HALF; break;
		default: break;
		}
		if (trim != 0)
			blit_sprite2(VGAScreen, trimX, y + 14, *sheet, trim);
	}
	else
		blit_sprite2x2(VGAScreen, cx - SHOP_WIDE_HULL_HALF, y, *sheet, gr + pose);
}

static bool seFromOutpost;

static EditorMusicTick seMusicTick(void)
{
	return editorMusicTick(seFromOutpost);
}

bool JE_shipEditorOutpostMusicSelfTest(void)
{
	const bool savedOutpost = seFromOutpost;
	seFromOutpost = true;
	const bool outpostKeepsSong = seMusicTick() == NULL;
	seFromOutpost = false;
	const bool setupShuffles = seMusicTick() == editorServiceMusic;
	seFromOutpost = savedOutpost;
	return outpostKeepsSong && setupShuffles;
}

static void sePickDraw(int graphic, int px, int py, int w, int h)
{
	spritePickerDrawGraphic(EXTRA_SHIPS_LOCAL_SEAT, graphic, px, py, w, h);
}

static bool sePickGraphic(int slot)
{
	int order[256], hulls[256], banks[256];
	const int count = seGraphicOrder(order);
	int hullCount = 0, bankCount = 0;
	for (int i = 0; i < count; ++i)
	{
		if (extraShipGraphicIsCustom(order[i]))
			banks[bankCount++] = order[i];
		else
			hulls[hullCount++] = order[i];
	}

	const SpritePickerPage pages[2] =
	{
		spritePickerHullPage("In-Game", hulls, hullCount, sePickDraw, extraShipGraphicLabel),
		spritePickerHullPage("Custom", banks, bankCount, sePickDraw, extraShipGraphicLabel),
	};
	const SpritePickerSpec spec = { .title = "Ship Graphic", .pages = pages, .pageCount = 2,
	                                .idle = editorServiceNetwork, .tick = seMusicTick() };
	int value = *seField(slot, SE_ROW_GRAPHIC);
	if (!spritePickerRun(&spec, &value) || !seValueOk(SE_ROW_GRAPHIC, value))
		return false;
	*seField(slot, SE_ROW_GRAPHIC) = (JE_byte)value;
	return true;
}

enum
{
	SE_BOX_X0 = 8, SE_BOX_Y0 = 8, SE_BOX_X1 = 143, SE_BOX_Y1 = 182,
	SE_POSES_Y = SE_BOX_Y0 + 22,
	SE_CENTER_Y = SE_BOX_Y0 + 70,
	SE_ITEMS_Y = SE_BOX_Y0 + 119,
	SE_KICK_L_X = SE_BOX_X0 + 5,
	SE_KICK_R_X = SE_BOX_X1 - 28,
};

enum
{
	SE_BG_SPACE = 0x50,
	SE_BG_BAND = 0x51,
	SE_BG_SOCKET = 0x53,
	SE_BG_RIM = 0x54,
	SE_BG_CLAMP = 0x55,
	SE_BG_SELECT = 0x5a,
	SE_BG_WELL = 0x60,
	SE_BG_STAR_COUNT = 32,
};

static Uint8 seBoxBase[SE_BOX_Y1 - SE_BOX_Y0 + 1][SE_BOX_X1 - SE_BOX_X0 + 1];

static void seCaptureBoxBase(void)
{
	const Uint8 *src = (const Uint8 *)VGAScreenSeg->pixels
	                 + SE_BOX_Y0 * VGAScreenSeg->pitch + SE_BOX_X0;
	for (int y = 0; y <= SE_BOX_Y1 - SE_BOX_Y0; ++y, src += VGAScreenSeg->pitch)
		memcpy(seBoxBase[y], src, SE_BOX_X1 - SE_BOX_X0 + 1);
}

static void seRestoreBoxBase(void)
{
	Uint8 *dst = (Uint8 *)VGAScreenSeg->pixels
	           + SE_BOX_Y0 * VGAScreenSeg->pitch + SE_BOX_X0;
	for (int y = 0; y <= SE_BOX_Y1 - SE_BOX_Y0; ++y, dst += VGAScreenSeg->pitch)
		memcpy(dst, seBoxBase[y], SE_BOX_X1 - SE_BOX_X0 + 1);
}

static bool seBayGlassAt(int x, int y)
{
	if (y < SE_CENTER_Y - 1 || y > SE_CENTER_Y + 28)
		return false;
	return (x >= SE_KICK_L_X - 1 && x <= SE_KICK_L_X + 24) ||
	       (x >= SE_KICK_R_X - 1 && x <= SE_KICK_R_X + 24);
}

static void seDrawStarCell(SDL_Surface *dst, int scale, int x_offset, int x, float fy, JE_byte c)
{
	if (x < SE_BOX_X0 || x > SE_BOX_X1)
		return;

	const int hx = (x + x_offset) * scale;
	const int hy = (int)(fy * (float)scale + 0.5f);

	for (int py = hy; py < hy + scale; ++py)
	{
		const int sy = py / scale;
		if (sy < SE_BOX_Y0 || sy > SE_BOX_Y1)
			continue;

		const Uint8 base = seBoxBase[sy - SE_BOX_Y0][x - SE_BOX_X0];
		JE_byte draw;
		if (base == SE_BG_SPACE)
			draw = c;
		else if (base == SE_BG_WELL && seBayGlassAt(x, sy))
			draw = (JE_byte)((c & 0xf0) | ((c & 0x0f) >> 1));
		else
			continue;
		memset((Uint8 *)dst->pixels + py * dst->pitch + hx, draw, (size_t)scale);
	}
}

static int seMountArtX(bool rightSide, bool big, uint sprite)
{
	const int mid = (SE_BOX_X0 + SE_BOX_X1 + 1) / 2;
	const int gap0 = rightSide ? mid + SHOP_WIDE_HULL_HALF : SE_KICK_L_X + 26;
	const int gap1 = rightSide ? SE_KICK_R_X - 3 : mid - SHOP_WIDE_HULL_HALF - 1;

	int a0, a1, top, bottom;
	spritePickerInkBox(big ? spriteSheet10 : spriteSheet9, sprite, big, &a0, &top, &a1, &bottom);
	return gap0 + (gap1 - gap0 + 1 - (a1 - a0 + 1) + (rightSide ? 1 : 0)) / 2 - a0;
}

typedef struct
{
	JE_byte id;
	uint frame;
	uint charge;
	uint charge_ticks;
	uint fire_ticks;
	bool animating;
} SeMountAnim;

static SeMountAnim seMountAnim[2];
static Uint64 seMountTicksDone;

static void seMountAnimSync(uint side, JE_byte v)
{
	SeMountAnim *const m = &seMountAnim[side];

	if (m->id == v)
		return;
	m->id = v;
	m->frame = 0;
	m->charge = 0;
	m->charge_ticks = 20;
	m->fire_ticks = 1;
	m->animating = true;
}

static void seMountAnimTick(SeMountAnim *m)
{
	const JE_OptionType *const o = &options[m->id];

	if (o->pwr == 0)
	{
		if (--m->fire_ticks == 0)
		{
			m->fire_ticks = (o->wpnum > 0 && o->wpnum <= WEAP_NUM)
			              ? (uint)weapons[o->wpnum].shotrepeat + 1 : 20;
			m->animating = true;
		}
	}

	if (m->animating)
	{
		if (++m->frame >= o->ani)
		{
			m->frame = 0;
			m->animating = (o->option == 1);
		}
	}

	if (o->pwr > 0)
	{
		if (--m->charge_ticks == 0)
		{
			if (m->charge < o->pwr)
				++m->charge;
			else
			{
				m->charge = 0;
				m->animating = true;
			}
			m->charge_ticks = 20;
		}
	}
}

static void seMountAnimAdvance(Uint32 clock_ms, JE_byte leftKick, JE_byte rightKick)
{
	seMountAnimSync(0, leftKick);
	seMountAnimSync(1, rightKick);

	const Uint64 target = (Uint64)clock_ms * 35 / 1000;
	if (seMountTicksDone > target || seMountTicksDone + 35 < target)
		seMountTicksDone = (target > 35) ? target - 35 : 0;
	for (; seMountTicksDone < target; ++seMountTicksDone)
		for (uint side = 0; side < 2; ++side)
		{
			const JE_byte v = seMountAnim[side].id;
			if (v != 0 && v <= OPTION_NUM && options[v].option != 0)
				seMountAnimTick(&seMountAnim[side]);
		}
}

static void seDrawMountBody(SDL_Surface *dst, int scale, int x_offset, bool rightSide,
                            float orbitAngle)
{
	const SeMountAnim *const m = &seMountAnim[rightSide ? 1 : 0];
	const JE_byte v = m->id;

	if (v == 0 || v > OPTION_NUM)
		return;
	const JE_OptionType *const o = &options[v];
	if (o->option == 0)
		return;

	const uint frame = (m->frame < COUNTOF(o->gr)) ? m->frame : 0;
	uint sprite = o->gr[frame];
	if (sprite == 0)
		return;
	sprite += m->charge;

	const bool big = (o->tr == 1 || o->tr == 2);
	const uint anchor = (o->gr[0] != 0) ? o->gr[0] : sprite;
	float fx = (float)seMountArtX(rightSide, big, anchor);
	float fy;
	switch (o->tr)
	{
	case 1:
		fy = SE_CENTER_Y + 4;
		break;
	case 2:
		fy = SE_CENTER_Y - 4;
		break;
	case 3:
		fy = SE_CENTER_Y + 16;
		break;
	case 4:
		fx += (rightSide ? -sinf(orbitAngle) : sinf(orbitAngle)) * 5.0f;
		fy = (float)(SE_CENTER_Y + 6) + (rightSide ? -cosf(orbitAngle) : cosf(orbitAngle)) * 5.0f;
		break;
	default:
		fy = SE_CENTER_Y + 7;
		break;
	}

	const int hx = (int)((fx + (float)x_offset) * (float)scale + 0.5f);
	const int hy = (int)(fy * (float)scale + 0.5f);
	if (big)
		blit_sprite2x2_scaled(dst, hx, hy, spriteSheet10, sprite, scale, BLIT2_COPY, 0);
	else
		blit_sprite2_scaled(dst, hx, hy, spriteSheet9, sprite, scale, BLIT2_COPY, 0);

	const int w = big ? 24 : 12, h = big ? 28 : 14;
	for (int y = (int)fy - 1; y <= (int)fy + h + 1; ++y)
		for (int x = (int)fx - 1; x <= (int)fx + w + 1; ++x)
		{
			if (x < SE_BOX_X0 || x > SE_BOX_X1 || y < SE_BOX_Y0 || y > SE_BOX_Y1)
				continue;
			const Uint8 p = seBoxBase[y - SE_BOX_Y0][x - SE_BOX_X0];
			if (p == SE_BG_SPACE)
				continue;
			Uint8 *row = (Uint8 *)dst->pixels + (y * scale) * dst->pitch + (x + x_offset) * scale;
			for (int k = 0; k < scale; ++k, row += dst->pitch)
				memset(row, p, (size_t)scale);
		}
}

static void seDrawPreviewBackdrop(void)
{
	fill_rectangle_xy(VGAScreen, SE_BOX_X0, SE_BOX_Y0, SE_BOX_X1, SE_BOX_Y1, SE_BG_SPACE);

	const int stripY0 = SE_POSES_Y - 3, stripY1 = SE_POSES_Y + 30;
	fill_rectangle_xy(VGAScreen, SE_BOX_X0, stripY0, SE_BOX_X1, stripY1, SE_BG_BAND);
	fill_rectangle_xy(VGAScreen, SE_BOX_X0, stripY0, SE_BOX_X1, stripY0, SE_BG_RIM);
	fill_rectangle_xy(VGAScreen, SE_BOX_X0, stripY1, SE_BOX_X1, stripY1, SE_BG_RIM);

	const int trayY0 = SE_ITEMS_Y - 5;
	fill_rectangle_xy(VGAScreen, SE_BOX_X0, trayY0, SE_BOX_X1, SE_BOX_Y1, SE_BG_BAND);
	fill_rectangle_xy(VGAScreen, SE_BOX_X0, trayY0, SE_BOX_X1, trayY0, SE_BG_RIM);

	const int cellY0 = SE_ITEMS_Y - 2, cellY1 = SE_ITEMS_Y + 29;
	for (int i = 0; i < 5; ++i)
	{
		const int cx0 = SE_BOX_X0 + i * 27, cx1 = cx0 + 27;
		fill_rectangle_xy(VGAScreen, cx0 + 1, cellY0 + 1, cx1 - 1, cellY1 - 1, SE_BG_WELL);
		JE_rectangle(VGAScreen, cx0, cellY0, cx1, cellY1, SE_BG_SOCKET);
	}

	fill_rectangle_xy(VGAScreen, SE_KICK_L_X - 1, SE_CENTER_Y - 1,
	                  SE_KICK_L_X + 24, SE_CENTER_Y + 28, SE_BG_WELL);
	fill_rectangle_xy(VGAScreen, SE_KICK_R_X - 1, SE_CENTER_Y - 1,
	                  SE_KICK_R_X + 24, SE_CENTER_Y + 28, SE_BG_WELL);
	draw_corner_clamps(VGAScreen, SE_KICK_L_X - 2, SE_CENTER_Y - 2, SE_KICK_L_X + 25,
	                   SE_CENTER_Y + 29, 4, SE_BG_CLAMP);
	draw_corner_clamps(VGAScreen, SE_KICK_R_X - 2, SE_CENTER_Y - 2, SE_KICK_R_X + 25,
	                   SE_CENTER_Y + 29, 4, SE_BG_CLAMP);
}

static EditorFlash seFlash;

static SDL_Surface *seHiFrame = NULL;

static void sePresentPreviewTick(Uint32 *clock_ms, Uint32 *last_ms,
                                 JE_byte leftKick, JE_byte rightKick)
{
	seCaptureBoxBase();

	const Uint16 mx0 = mouse_x, my0 = mouse_y;
	bool first = true;

	for (;;)
	{
		if (!first && getDelayTicks() == 0)
			break;
		first = false;

		const Uint32 now = SDL_GetTicks();
		*clock_ms += now - *last_ms;
		*last_ms = now;

		seMountAnimAdvance(*clock_ms, leftKick, rightKick);
		const bool bothSat = leftKick != 0 && leftKick <= OPTION_NUM && options[leftKick].tr == 4 &&
		                     rightKick != 0 && rightKick <= OPTION_NUM && options[rightKick].tr == 4;
		const float orbitAngle =
			(float)fmod((double)*clock_ms * 0.035 * (bothSat ? 0.2 : 0.15), 6.283185307179586);

		const int ss = effective_supersample();
		SDL_Surface *const hi = ss > 1 ? video_ensure_scaled_surface(&seHiFrame, ss) : NULL;

		seRestoreBoxBase();
		if (hi == NULL)
		{
			if (shipEditorStars)
				editorDrawStars(VGAScreen, 1, 0, *clock_ms, SE_BOX_X0, SE_BOX_Y0, SE_BOX_X1,
				                SE_BOX_Y1, SE_BG_STAR_COUNT, seDrawStarCell);
			seDrawMountBody(VGAScreen, 1, 0, false, orbitAngle);
			seDrawMountBody(VGAScreen, 1, 0, true, orbitAngle);
			editorDrawFlash(&seFlash, VGAScreen, 1, 0);
		}

		JE_mouseStart();
		if (hi != NULL)
		{
			const int x_offset = video_get_menu_x_offset();
			expand_frame_to_hi(video_compose_frame(), hi, ss);
			if (shipEditorStars)
				editorDrawStars(hi, ss, x_offset, *clock_ms, SE_BOX_X0, SE_BOX_Y0, SE_BOX_X1,
				                SE_BOX_Y1, SE_BG_STAR_COUNT, seDrawStarCell);
			seDrawMountBody(hi, ss, x_offset, false, orbitAngle);
			seDrawMountBody(hi, ss, x_offset, true, orbitAngle);
			editorDrawFlash(&seFlash, hi, ss, x_offset);
			JE_drawMouseToHiFrame(hi, ss, x_offset);
			present_hi(hi);
		}
		else
		{
			JE_showVGA();
		}
		JE_mouseReplace();

		if (newkey || newmouse || mouse_scroll != 0 || mouse_x != mx0 || mouse_y != my0)
			break;
		if (!output_vsync)
			limit_render_fps();
	}
}

static void seSeedDefaults(void)
{
	memset(extraShips, 0, sizeof(extraShips));
	for (int slot = 1; slot <= 10; ++slot)
	{
		*seField(slot, SE_ROW_GRAPHIC) = (JE_byte)(1 + (slot - 1) % 7);
		*seField(slot, SE_ROW_FRONT) = 1;      // Pulse-Cannon
		*seField(slot, SE_ROW_GENERATOR) = 1;
		*seField(slot, SE_ROW_ARMOR) = 10;
		*seField(slot, SE_ROW_SHIELD) = 1;
	}
}

static void seNormalizeShips(void)
{
	for (int slot = 1; slot <= 10; ++slot)
		for (int row = SE_ROW_GRAPHIC; row < SE_ROW_COUNT; ++row)
			if (!seValueOk(row, *seField(slot, row)))
				seStepField(slot, row, row == SE_ROW_ARMOR ? -1 : 1);
}

// The editor expands the stock Sprite2 layout into flat 12x14 cells.

enum { SE_CELL_W = 12, SE_CELL_H = 14, SE_CELL_BYTES = SE_CELL_W * SE_CELL_H };
enum { SE_FRAME_W = 24, SE_FRAME_H = 28 };
enum { SE_BANKS = 8, SE_FRAMES = 5 };
enum { SE_BLOB_SPRITES = 304 };

// Sprite2 base indices for graphics 8 through 15.
static const JE_word seBankBase[SE_BANKS] = { 5, 43, 81, 119, 157, 195, 233, 271 };

static JE_byte seCells[SE_BLOB_SPRITES + 1][SE_CELL_BYTES];       // [1..SE_BLOB_SPRITES]
static JE_byte seCellsSaved[SE_BLOB_SPRITES + 1][SE_CELL_BYTES];

// Corners 0/1 are top-left/right; 2/3 are bottom-left/right.
static unsigned seHullCellIndex(unsigned base, int frame, int corner)
{
	return base + (frame - 3) * 2 + (corner & 1) + (corner >= 2 ? 19 : 0);
}

static unsigned seCellIndex(int bank, int frame, int corner)
{
	return seHullCellIndex(seBankBase[bank - 1], frame, corner);
}

static bool seSpriteCellValid(const Sprite2_array *sheet, unsigned index)
{
	return sheet->data != NULL && index >= 1 &&
	       (size_t)index * sizeof(Uint16) <= sheet->size &&
	       SDL_SwapLE16(((const Uint16 *)sheet->data)[index - 1]) < sheet->size;
}

// Decode the Sprite2 stream into a flat cell; color 0 stays transparent.
static void seDecodeCell(const Sprite2_array *sheet, unsigned index, JE_byte *out)
{
	memset(out, 0, SE_CELL_BYTES);
	if (!seSpriteCellValid(sheet, index))
		return;
	const Uint16 off = SDL_SwapLE16(((const Uint16 *)sheet->data)[index - 1]);

	const Uint8 *data = sheet->data + off;
	const Uint8 *const end = sheet->data + sheet->size;
	unsigned pos = 0;
	for (; data < end && *data != 0x0f && pos < SE_CELL_BYTES; ++data)
	{
		pos += *data & 0x0f;
		unsigned count = *data >> 4;
		while (count-- && ++data < end && pos < SE_CELL_BYTES)
			out[pos++] = *data;
	}
}

// A 12-pixel row keeps every run and skip within one nibble.
static size_t seEncodeCell(const JE_byte *px, Uint8 *buf)
{
	size_t n = 0;
	for (int y = 0; y < SE_CELL_H; ++y)
	{
		const JE_byte *const row = px + y * SE_CELL_W;
		int x = 0, skip = 0;
		while (x < SE_CELL_W)
		{
			if (row[x] == 0)
			{
				++skip;
				++x;
				continue;
			}
			int run = 0;
			while (x + run < SE_CELL_W && row[x + run] != 0)
				++run;
			buf[n++] = (Uint8)((run << 4) | skip);
			memcpy(&buf[n], &row[x], run);
			n += run;
			x += run;
			skip = 0;
		}
		buf[n++] = (Uint8)skip;
	}
	buf[n++] = 0x0f;
	return n;
}

// Rebuild the offset table and cell streams within the format's 16-bit limit.
static void seRebuildShapes(void)
{
	Uint8 *const blob = malloc_die(UINT16_MAX + 1);
	size_t n = SE_BLOB_SPRITES * sizeof(Uint16);
	for (unsigned i = 1; i <= SE_BLOB_SPRITES; ++i)
	{
		((Uint16 *)blob)[i - 1] = SDL_SwapLE16((Uint16)n);
		n += seEncodeCell(seCells[i], blob + n);
	}

	free_sprite2s(&extraShapes);
	extraShapes.size = n;
	extraShapes.data = malloc_die(n);
	memcpy(extraShapes.data, blob, n);
	free(blob);
}

// Keep defaults in memory so Revert can recover the editor's opening state.
static bool seLoadStockDefaults(void)
{
	FILE *const f = dir_fopen(data_dir(), EXTRA_SHIPS_STOCK_FILE, "rb");
	if (f == NULL)
		return false;

	JE_ShipsType table;
	Sprite2_array shapes = {0, NULL};
	const bool loaded = extraShipsReadCompiled(f, table, &shapes);
	fclose(f);
	if (!loaded)
		return false;

	memcpy(extraShips, table, sizeof(extraShips));
	free_sprite2s(&extraShapes);
	extraShapes = shapes;
	extraAvail = true;
	extraUserAvail = false;
	for (unsigned i = 1; i <= SE_BLOB_SPRITES; ++i)
		seDecodeCell(&extraShapes, i, seCells[i]);
	seNormalizeShips();
	return true;
}

// Compile User.shp's sparse cells into the runtime Sprite2 sheet. See doc/notes.md#extra-ships.
static bool seImportLegacyUserShapes(FILE *f)
{
	const long fileSize = ftell_eof(f);
	const long minSize = SE_BLOB_SPRITES + (long)sizeof(JE_ShipsType);
	const long maxSize = SE_BLOB_SPRITES * (1 + SE_CELL_BYTES) + (long)sizeof(JE_ShipsType);
	if (fileSize < minSize || fileSize > maxSize)
		return false;

	JE_byte (*const importedCells)[SE_CELL_BYTES] = calloc(SE_BLOB_SPRITES + 1, SE_CELL_BYTES);
	if (importedCells == NULL)
		return false;

	JE_ShipsType encrypted, savedShips;
	rewind(f);
	for (unsigned i = 1; i <= SE_BLOB_SPRITES; ++i)
	{
		const int present = fgetc(f);
		if (present == EOF ||
		    (present != 0 && fread(importedCells[i], 1, SE_CELL_BYTES, f) != SE_CELL_BYTES))
		{
			free(importedCells);
			return false;
		}
	}
	if (fread(encrypted, 1, sizeof(encrypted), f) != sizeof(encrypted) ||
	    fgetc(f) != EOF || ferror(f))
	{
		free(importedCells);
		return false;
	}

	memcpy(savedShips, extraShips, sizeof(savedShips));
	memcpy(extraShips, encrypted, sizeof(extraShips));
	if (!JE_decryptShips())
	{
		memcpy(extraShips, savedShips, sizeof(extraShips));
		free(importedCells);
		return false;
	}

	memcpy(seCells, importedCells, sizeof(seCells));
	free(importedCells);
	seRebuildShapes();
	extraAvail = true;
	extraUserAvail = true;
	return true;
}

bool JE_legacyUserShapeSelfTest(void)
{
	FILE *source = dir_fopen(data_dir(), "user1.shp", "rb");
	FILE *compiled = dir_fopen(data_dir(), EXTRA_SHIPS_STOCK_FILE, "rb");
	if (source == NULL || compiled == NULL)
	{
		if (source != NULL)
			fclose(source);
		if (compiled != NULL)
			fclose(compiled);
		return false;
	}

	JE_ShipsType savedShips;
	memcpy(savedShips, extraShips, sizeof(savedShips));
	const Sprite2_array savedShapes = extraShapes;
	const bool savedAvail = extraAvail, savedUserAvail = extraUserAvail;
	extraShapes = (Sprite2_array){ 0, NULL };

	bool ok = seImportLegacyUserShapes(source);
	if (ok)
	{
		static const JE_byte firstRecord[15] = {
			8, 6, 0, 6, 11, 11, 3, 17, 6, 17, 18, 19, 28, 29, 1
		};
		ok = memcmp(extraShips, firstRecord, sizeof(firstRecord)) == 0;
	}

	Sprite2_array expected = { 0, NULL };
	const long compiledSize = ftell_eof(compiled);
	if (ok && compiledSize > (long)sizeof(JE_ShipsType))
	{
		expected.size = (size_t)(compiledSize - (long)sizeof(JE_ShipsType));
		expected.data = malloc(expected.size);
		ok = expected.data != NULL;
		if (ok)
		{
			rewind(compiled);
			ok = fread(expected.data, 1, expected.size, compiled) == expected.size;
		}
	}
	else
		ok = false;

	JE_byte actualCell[SE_CELL_BYTES], expectedCell[SE_CELL_BYTES];
	for (unsigned i = 1; ok && i <= SE_BLOB_SPRITES; ++i)
	{
		seDecodeCell(&extraShapes, i, actualCell);
		seDecodeCell(&expected, i, expectedCell);
		ok = memcmp(actualCell, expectedCell, SE_CELL_BYTES) == 0;
	}

	// An explicit User.shp import is immediately eligible for online/custom-data transfer.
	Uint8 *const wire = malloc(EXTRA_SHIPS_WIRE_MAX);
	if (ok && wire != NULL)
	{
		const size_t wireSize = extraShipsSerializeUser(wire, EXTRA_SHIPS_WIRE_MAX);
		ok = wireSize > 6 + sizeof(JE_ShipsType) && wire[1] == 1 &&
		     extraShipsPayloadValid(wire, wireSize);
	}
	else
		ok = false;
	free(wire);

	fclose(source);
	fclose(compiled);
	free_sprite2s(&expected);
	free_sprite2s(&extraShapes);
	extraShapes = savedShapes;
	memcpy(extraShips, savedShips, sizeof(extraShips));
	extraAvail = savedAvail;
	extraUserAvail = savedUserAvail;
	return ok;
}

// A custom bank holds one 24x28 hull, so two-piece hulls cannot be captured.
static bool seCaptureHullInfo(int ship, Sprite2_array **sheet, JE_word *base)
{
	if (ship < 1 || ship > SHIP_DRAGONWING || ships[ship].name[0] == '\0' ||
	    ships[ship].shipgraphic <= 1)
		return false;

	const bool t2000 = ships[ship].shipgraphic > 500;
	*sheet = t2000 ? &spriteSheetT2000 : &spriteSheet9;
	*base = ships[ship].shipgraphic - (t2000 ? 500 : 0);

	for (int frame = 1; frame <= SE_FRAMES; ++frame)
	{
		bool painted = false;
		for (int corner = 0; corner < 4; ++corner)
		{
			const unsigned index = seHullCellIndex(*base, frame, corner);
			if (!seSpriteCellValid(*sheet, index))
				return false;
			painted = painted || !sprite2_is_blank(**sheet, index);
		}
		if (!painted)
			return false;
	}
	return true;
}

static bool seCaptureHullOk(int ship)
{
	Sprite2_array *sheet;
	JE_word base;
	return seCaptureHullInfo(ship, &sheet, &base);
}

bool JE_captureHullListSelfTest(void)
{
	char trimmed[16];
	if (strcmp(JE_trimItemName("  Rum Bottle   ", trimmed, sizeof(trimmed)), "Rum Bottle") != 0)
		return false;

	JE_byte list[SHIP_DRAGONWING];
	const int count = JE_distinctShipHulls(list, COUNTOF(list), seCaptureHullOk);
	if (count <= 0)
		return false;

	for (int i = 0; i < count; ++i)
	{
		Sprite2_array *sheet;
		JE_word base;
		if (!seCaptureHullInfo(list[i], &sheet, &base))
			return false;
		for (int j = 0; j < i; ++j)
			if (ships[list[i]].shipgraphic == ships[list[j]].shipgraphic)
				return false;
	}

	int uniqueCompatible = 0;
	JE_word seen[SHIP_DRAGONWING];
	for (int ship = 1; ship <= SHIP_DRAGONWING; ++ship)
	{
		Sprite2_array *sheet;
		JE_word base;
		if (!seCaptureHullInfo(ship, &sheet, &base))
			continue;

		const JE_word raw = ships[ship].shipgraphic;
		bool duplicate = false;
		for (int i = 0; i < uniqueCompatible; ++i)
			duplicate = duplicate || seen[i] == raw;
		if (!duplicate)
			seen[uniqueCompatible++] = raw;
	}
	return count == uniqueCompatible;
}

static bool seCaptureHull(int bank, int sourceShip, int first, int last)
{
	Sprite2_array *sheet;
	JE_word base;
	if (!seCaptureHullInfo(sourceShip, &sheet, &base))
		return false;

	for (int frame = first; frame <= last; ++frame)
		for (int corner = 0; corner < 4; ++corner)
		{
			const unsigned src = seHullCellIndex(base, frame, corner);
			seDecodeCell(sheet, src, seCells[seCellIndex(bank, frame, corner)]);
		}
	return true;
}

static int seCustomSourceBank(int index, int bank)
{
	return (index + 1 < bank) ? index + 1 : index + 2;
}

static void seCaptureCustom(int bank, int sourceBank, int first, int last)
{
	for (int frame = first; frame <= last; ++frame)
		for (int corner = 0; corner < 4; ++corner)
			memcpy(seCells[seCellIndex(bank, frame, corner)],
			       seCells[seCellIndex(sourceBank, frame, corner)], SE_CELL_BYTES);
}

// Round-trip a worst-case cell and every loaded cell.
bool JE_shapeCodecSelfTest(void)
{
	JE_byte cell[SE_CELL_BYTES], back[SE_CELL_BYTES];
	Uint8 buf[SE_CELL_W * SE_CELL_H * 2];

	for (int i = 0; i < SE_CELL_BYTES; ++i)  // stripes, solid rows, blank rows, lone edge pixels
	{
		const int x = i % SE_CELL_W, y = i / SE_CELL_W;
		cell[i] = (JE_byte)(y % 4 == 3 ? 0 : (y % 4 == 2 ? (x == SE_CELL_W - 1 ? 7 : 0) : (x % 2 ? y + 1 : 0)));
	}
	Sprite2_array one = { 0, NULL };
	one.size = sizeof(Uint16) + seEncodeCell(cell, buf + sizeof(Uint16));
	((Uint16 *)buf)[0] = SDL_SwapLE16((Uint16)sizeof(Uint16));
	one.data = buf;
	seDecodeCell(&one, 1, back);
	if (memcmp(cell, back, SE_CELL_BYTES) != 0)
		return false;

	if (extraShapes.size > 0)
	{
		for (unsigned i = 1; i <= SE_BLOB_SPRITES; ++i)
			seDecodeCell(&extraShapes, i, seCells[i]);
		seRebuildShapes();
		for (unsigned i = 1; i <= SE_BLOB_SPRITES; ++i)
		{
			seDecodeCell(&extraShapes, i, back);
			if (memcmp(seCells[i], back, SE_CELL_BYTES) != 0)
				return false;
		}
	}
	return true;
}

/* Sprite editor */

static JE_byte *seFramePx(int bank, int frame, int x, int y)  // x 0..23, y 0..27
{
	const int corner = (x >= SE_CELL_W ? 1 : 0) + (y >= SE_CELL_H ? 2 : 0);
	return &seCells[seCellIndex(bank, frame, corner)][(y % SE_CELL_H) * SE_CELL_W + (x % SE_CELL_W)];
}

static void seFlipFrame(int bank, int frame, bool vertical)
{
	JE_byte tmp[SE_FRAME_W * SE_FRAME_H];
	for (int y = 0; y < SE_FRAME_H; ++y)
		for (int x = 0; x < SE_FRAME_W; ++x)
			tmp[y * SE_FRAME_W + x] = *seFramePx(bank, frame,
			                                     vertical ? x : SE_FRAME_W - 1 - x,
			                                     vertical ? SE_FRAME_H - 1 - y : y);
	for (int y = 0; y < SE_FRAME_H; ++y)
		for (int x = 0; x < SE_FRAME_W; ++x)
			*seFramePx(bank, frame, x, y) = tmp[y * SE_FRAME_W + x];
}

static void seFoldFrame(int bank, int frame, bool vertical, bool fromFar)
{
	if (vertical)
	{
		for (int y = 0; y < SE_FRAME_H / 2; ++y)
			for (int x = 0; x < SE_FRAME_W; ++x)
			{
				const int src = fromFar ? SE_FRAME_H - 1 - y : y;
				const int dst = fromFar ? y : SE_FRAME_H - 1 - y;
				*seFramePx(bank, frame, x, dst) = *seFramePx(bank, frame, x, src);
			}
		return;
	}

	for (int y = 0; y < SE_FRAME_H; ++y)
		for (int x = 0; x < SE_FRAME_W / 2; ++x)
		{
			const int src = fromFar ? SE_FRAME_W - 1 - x : x;
			const int dst = fromFar ? x : SE_FRAME_W - 1 - x;
			*seFramePx(bank, frame, dst, y) = *seFramePx(bank, frame, src, y);
		}
}

static void seNudgeFrame(int bank, int frame, int dx, int dy)
{
	JE_byte tmp[SE_FRAME_W * SE_FRAME_H] = { 0 };
	for (int y = 0; y < SE_FRAME_H; ++y)
		for (int x = 0; x < SE_FRAME_W; ++x)
		{
			const int toX = x + dx, toY = y + dy;
			if (toX >= 0 && toX < SE_FRAME_W && toY >= 0 && toY < SE_FRAME_H)
				tmp[toY * SE_FRAME_W + toX] = *seFramePx(bank, frame, x, y);
		}
	for (int y = 0; y < SE_FRAME_H; ++y)
		for (int x = 0; x < SE_FRAME_W; ++x)
			*seFramePx(bank, frame, x, y) = tmp[y * SE_FRAME_W + x];
}

static void seDrawFramePx(int x0, int y0, int bank, int frame)
{
	for (int y = 0; y < SE_FRAME_H; ++y)
	{
		Uint8 *const row = (Uint8 *)VGAScreen->pixels + (size_t)(y0 + y) * VGAScreen->pitch + x0;
		for (int x = 0; x < SE_FRAME_W; ++x)
		{
			const JE_byte c = *seFramePx(bank, frame, x, y);
			if (c != 0)
				row[x] = c;
		}
	}
}

static void seDrawArrow(int x0, int y0, int direction, JE_byte color)
{
	static const Uint8 pixels[4][7] = {
		{ 0x08, 0x0C, 0x0E, 0x7F, 0x0E, 0x0C, 0x08 },  // left
		{ 0x08, 0x1C, 0x3E, 0x7F, 0x08, 0x08, 0x08 },  // up
		{ 0x08, 0x08, 0x08, 0x7F, 0x3E, 0x1C, 0x08 },  // down
		{ 0x08, 0x18, 0x38, 0x7F, 0x38, 0x18, 0x08 },  // right
	};
	Uint8 *const dst = VGAScreen->pixels;
	for (int y = 0; y < 7; ++y)
		for (int x = 0; x < 7; ++x)
			if ((pixels[direction][y] & (1u << x)) != 0)
				dst[(y0 + y) * VGAScreen->pitch + x0 + x] = color;
}

enum { SE_UNDO_DEPTH = 4096 };

typedef struct
{
	JE_byte bank;
	JE_byte cells[SE_FRAMES * 4][SE_CELL_BYTES];
} SeUndoState;

static SeUndoState seUndoPool[SE_UNDO_DEPTH];
static int seUndoBase, seUndoCount, seRedoCount;

static void seUndoCapture(int bank, SeUndoState *out)
{
	out->bank = (JE_byte)bank;
	for (int frame = 1; frame <= SE_FRAMES; ++frame)
		for (int corner = 0; corner < 4; ++corner)
			memcpy(out->cells[(frame - 1) * 4 + corner],
			       seCells[seCellIndex(bank, frame, corner)], SE_CELL_BYTES);
}

static void seUndoRestore(const SeUndoState *s)
{
	for (int frame = 1; frame <= SE_FRAMES; ++frame)
		for (int corner = 0; corner < 4; ++corner)
			memcpy(seCells[seCellIndex(s->bank, frame, corner)],
			       s->cells[(frame - 1) * 4 + corner], SE_CELL_BYTES);
}

static bool seUndoMatches(const SeUndoState *s)
{
	for (int frame = 1; frame <= SE_FRAMES; ++frame)
		for (int corner = 0; corner < 4; ++corner)
			if (memcmp(s->cells[(frame - 1) * 4 + corner],
			           seCells[seCellIndex(s->bank, frame, corner)], SE_CELL_BYTES) != 0)
				return false;
	return true;
}

static void seUndoSwap(SeUndoState *s)
{
	SeUndoState live;
	seUndoCapture(s->bank, &live);
	seUndoRestore(s);
	*s = live;
}

static void seUndoBegin(int bank)
{
	seRedoCount = 0;
	if (seUndoCount > 0)
	{
		const SeUndoState *top = &seUndoPool[(seUndoBase + seUndoCount - 1) % SE_UNDO_DEPTH];
		if (top->bank == bank && seUndoMatches(top))
			return;
	}
	if (seUndoCount == SE_UNDO_DEPTH)
	{
		seUndoBase = (seUndoBase + 1) % SE_UNDO_DEPTH;
		--seUndoCount;
	}
	seUndoCapture(bank, &seUndoPool[(seUndoBase + seUndoCount++) % SE_UNDO_DEPTH]);
}

static bool seUndoApply(bool redo, int *bank)
{
	if (redo)
	{
		while (seRedoCount > 0)
		{
			SeUndoState *s = &seUndoPool[(seUndoBase + seUndoCount) % SE_UNDO_DEPTH];
			++seUndoCount;
			--seRedoCount;
			if (seUndoMatches(s))
				continue;
			seUndoSwap(s);
			*bank = s->bank;
			return true;
		}
		return false;
	}
	while (seUndoCount > 0)
	{
		SeUndoState *s = &seUndoPool[(seUndoBase + seUndoCount - 1) % SE_UNDO_DEPTH];
		--seUndoCount;
		++seRedoCount;
		if (seUndoMatches(s))
			continue;
		seUndoSwap(s);
		*bank = s->bank;
		return true;
	}
	return false;
}

enum { SE_STRIP_W = SE_FRAME_W * SE_FRAMES };
enum { SE_STRIP_PNG_MAX_BYTES = 8192, SE_STRIP_FILE_MAX_BYTES = 1 << 20 };

typedef struct
{
	JE_byte px[SE_FRAME_H][SE_STRIP_W];
}
SeStrip;

typedef enum
{
	SE_IMAGE_OK,
	SE_IMAGE_MISSING,
	SE_IMAGE_UNREADABLE,
	SE_IMAGE_WRONG_SIZE,
}
SeImageResult;

static void seStripImageName(int bank, char *buf, size_t bufSize)
{
	snprintf(buf, bufSize, "custom-bank-%d.png", bank);
}

static void seStripCapture(int bank, SeStrip *strip)
{
	for (int frame = 1; frame <= SE_FRAMES; ++frame)
		for (int y = 0; y < SE_FRAME_H; ++y)
			for (int x = 0; x < SE_FRAME_W; ++x)
				strip->px[y][(frame - 1) * SE_FRAME_W + x] = *seFramePx(bank, frame, x, y);
}

static void seStripApply(int bank, const SeStrip *strip)
{
	for (int frame = 1; frame <= SE_FRAMES; ++frame)
		for (int y = 0; y < SE_FRAME_H; ++y)
			for (int x = 0; x < SE_FRAME_W; ++x)
				*seFramePx(bank, frame, x, y) = strip->px[y][(frame - 1) * SE_FRAME_W + x];
}

static bool seSameRgb(SDL_Color a, SDL_Color b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b;
}

static bool seInkShared(JE_byte ink)
{
	for (int i = 1; i < 256; ++i)
		if (i != ink && seSameRgb(palettes[0][i], palettes[0][ink]))
			return true;
	return false;
}

static SDL_Color seNudged(SDL_Color c, int step)
{
	Uint8 *const channels[3] = { &c.b, &c.g, &c.r };
	Uint8 *const channel = channels[(step - 1) / 2 % 3];
	const int delta = (step + 5) / 6;
	const int nudged = *channel + ((step % 2 != 0) ? delta : -delta);
	if (nudged >= 0 && nudged <= 255)
		*channel = (Uint8)nudged;
	return c;
}

static bool seImageColorFree(SDL_Color c, const SDL_Color *image, int count)
{
	for (int i = 0; i < 256; ++i)
		if (seSameRgb(c, palettes[0][i]) || (i < count && seSameRgb(c, image[i])))
			return false;
	return true;
}

static void seImagePalette(SDL_Color *image)
{
	for (int i = 0; i < 256; ++i)
	{
		image[i] = palettes[0][i];
		if (i > 0 && seInkShared((JE_byte)i))
			for (int step = 1; !seImageColorFree(image[i], image, i); ++step)
				image[i] = seNudged(palettes[0][i], step);
		image[i].a = SDL_ALPHA_OPAQUE;
	}
}

static int seNearestInks(SDL_Color c, JE_byte *inks)
{
	const int nearest = palette_distance(c, palettes[0][palette_nearest(palettes[0], 1, c)]);
	int count = 0;
	for (int i = 1; i < 256; ++i)
		if (palette_distance(c, palettes[0][i]) == nearest)
			inks[count++] = (JE_byte)i;
	return count;
}

static SDL_Color seSurfacePixel(const SDL_Surface *s, int x, int y, int *index)
{
	const Uint8 *const row = (const Uint8 *)s->pixels + (size_t)y * s->pitch;
	const SDL_Palette *const pal = s->format->palette;
	if (pal != NULL)
	{
		*index = row[x];
		return (row[x] < pal->ncolors) ? pal->colors[row[x]]
		                               : (SDL_Color){ 0, 0, 0, SDL_ALPHA_TRANSPARENT };
	}

	SDL_Color c;
	*index = -1;
	SDL_GetRGBA(((const Uint32 *)row)[x], s->format, &c.r, &c.g, &c.b, &c.a);
	return c;
}

static JE_byte seImportPixel(SDL_Color c, int index, const SDL_Color *image, bool *unsure)
{
	*unsure = false;
	if (c.a < SDL_ALPHA_OPAQUE / 2 || seSameRgb(c, palettes[0][0]))
		return 0;
	if (index > 0 && seSameRgb(c, palettes[0][index]))
		return (JE_byte)index;
	for (int i = 1; i < 256; ++i)
		if (seSameRgb(c, image[i]))
			return (JE_byte)i;

	JE_byte inks[255];
	*unsure = seNearestInks(c, inks) > 1;
	return inks[0];
}

static int seBankVotes(const SeStrip *strip, bool (*unsure)[SE_STRIP_W], int x, int y, int bank)
{
	int votes = 0;
	for (int ny = SDL_max(y - 1, 0); ny <= SDL_min(y + 1, SE_FRAME_H - 1); ++ny)
		for (int nx = SDL_max(x - 1, 0); nx <= SDL_min(x + 1, SE_STRIP_W - 1); ++nx)
			if (!unsure[ny][nx] && strip->px[ny][nx] != 0 && (strip->px[ny][nx] >> 4) == bank)
				++votes;
	return votes;
}

static JE_byte seResolveInk(SDL_Color c, JE_byte was, const SeStrip *strip,
                            bool (*unsure)[SE_STRIP_W], const int *bankUse, int x, int y)
{
	JE_byte inks[255];
	const int count = seNearestInks(c, inks);
	for (int k = 0; k < count; ++k)
		if (inks[k] == was)
			return was;

	int best = 0, bestLocal = -1, bestGlobal = -1;
	for (int k = 0; k < count; ++k)
	{
		const int bank = inks[k] >> 4;
		const int local = seBankVotes(strip, unsure, x, y, bank), global = bankUse[bank];
		if (local > bestLocal || (local == bestLocal && global > bestGlobal))
		{
			best = k;
			bestLocal = local;
			bestGlobal = global;
		}
	}
	return inks[best];
}

static bool seStripFromSurface(SDL_Surface *s, const SeStrip *hint, SeStrip *strip)
{
	const bool indexed = s->format->format == SDL_PIXELFORMAT_INDEX8 && s->format->palette != NULL;
	SDL_Surface *const src = indexed ? s : SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
	if (src == NULL)
		return false;

	SDL_Color image[256];
	seImagePalette(image);
	bool unsure[SE_FRAME_H][SE_STRIP_W];
	int bankUse[16] = { 0 };
	for (int y = 0; y < SE_FRAME_H; ++y)
		for (int x = 0; x < SE_STRIP_W; ++x)
		{
			int index;
			const SDL_Color c = seSurfacePixel(src, x, y, &index);
			const JE_byte ink = seImportPixel(c, index, image, &unsure[y][x]);
			strip->px[y][x] = ink;
			if (ink != 0 && !unsure[y][x])
				++bankUse[ink >> 4];
		}

	for (int y = 0; y < SE_FRAME_H; ++y)
		for (int x = 0; x < SE_STRIP_W; ++x)
		{
			if (!unsure[y][x])
				continue;
			int index;
			const SDL_Color c = seSurfacePixel(src, x, y, &index);
			const JE_byte was = (hint != NULL) ? hint->px[y][x] : 0;
			strip->px[y][x] = seResolveInk(c, was, strip, unsure, bankUse, x, y);
		}

	if (src != s)
		SDL_FreeSurface(src);
	return true;
}

static SDL_Surface *seStripCanvas(Uint32 format)
{
	return SDL_CreateRGBSurfaceWithFormat(0, SE_STRIP_W, SE_FRAME_H, SDL_BITSPERPIXEL(format),
	                                      format);
}

static SDL_Surface *seStripSurface(const SeStrip *strip, const SDL_Color *colors)
{
	SDL_Surface *const s = seStripCanvas(SDL_PIXELFORMAT_INDEX8);
	if (s == NULL)
		return NULL;
	if (SDL_SetPaletteColors(s->format->palette, colors, 0, 256) != 0)
	{
		SDL_FreeSurface(s);
		return NULL;
	}

	for (int y = 0; y < SE_FRAME_H; ++y)
		memcpy((Uint8 *)s->pixels + (size_t)y * s->pitch, strip->px[y], SE_STRIP_W);
	return s;
}

static size_t seStripEncodePng(const SeStrip *strip, Uint8 *out, size_t cap)
{
	SDL_Color image[256];
	seImagePalette(image);
	image[0].a = SDL_ALPHA_TRANSPARENT;

	SDL_Surface *const s = seStripSurface(strip, image);
	const size_t size = (s != NULL) ? image_png_save(s, out, cap) : 0;
	SDL_FreeSurface(s);
	return size;
}

static SeImageResult seStripDecodePng(const Uint8 *bytes, size_t size, const SeStrip *hint,
                                      SeStrip *strip)
{
	SDL_Surface *const s = image_png_load(bytes, size);
	if (s == NULL)
		return SE_IMAGE_UNREADABLE;

	SeImageResult result = SE_IMAGE_WRONG_SIZE;
	if (s->w == SE_STRIP_W && s->h == SE_FRAME_H)
		result = seStripFromSurface(s, hint, strip) ? SE_IMAGE_OK : SE_IMAGE_UNREADABLE;
	SDL_FreeSurface(s);
	return result;
}

static bool seStripExportFile(int bank, char *notice, size_t noticeSize)
{
	char name[24];
	seStripImageName(bank, name, sizeof(name));

	SeStrip strip;
	seStripCapture(bank, &strip);
	Uint8 png[SE_STRIP_PNG_MAX_BYTES];
	const size_t size = seStripEncodePng(&strip, png, sizeof(png));
	FILE *const f = (size > 0) ? dir_fopen(make_user_directory(), name, "wb") : NULL;
	bool saved = false;
	if (f != NULL)
	{
		saved = fwrite(png, 1, size, f) == size;
		saved = fclose(f) == 0 && saved;
	}

	if (saved)
		snprintf(notice, noticeSize, "Saved %s in the save folder", name);
	else
		snprintf(notice, noticeSize, "Could not save %s", name);
	return saved;
}

static SeImageResult seStripReadFile(const char *name, const SeStrip *hint, SeStrip *strip)
{
	FILE *const f = dir_fopen(get_user_directory(), name, "rb");
	if (f == NULL)
		return SE_IMAGE_MISSING;

	const long size = ftell_eof(f);
	const bool sized = size > 0 && size <= SE_STRIP_FILE_MAX_BYTES;
	Uint8 *const bytes = sized ? malloc((size_t)size) : NULL;
	const bool read = bytes != NULL && fread(bytes, 1, (size_t)size, f) == (size_t)size;
	fclose(f);

	const SeImageResult result = read ? seStripDecodePng(bytes, (size_t)size, hint, strip)
	                                  : SE_IMAGE_UNREADABLE;
	free(bytes);
	return result;
}

static bool seStripImportFile(int bank, char *notice, size_t noticeSize)
{
	char name[24];
	seStripImageName(bank, name, sizeof(name));

	SeStrip current, strip;
	seStripCapture(bank, &current);
	const SeImageResult result = seStripReadFile(name, &current, &strip);
	switch (result)
	{
	case SE_IMAGE_OK:
		seUndoBegin(bank);
		seStripApply(bank, &strip);
		snprintf(notice, noticeSize, "Loaded %s", name);
		break;
	case SE_IMAGE_MISSING:
		snprintf(notice, noticeSize, "No %s in the save folder", name);
		break;
	case SE_IMAGE_WRONG_SIZE:
		snprintf(notice, noticeSize, "%s must be %dx%d", name, SE_STRIP_W, SE_FRAME_H);
		break;
	default:
		snprintf(notice, noticeSize, "Could not read %s", name);
		break;
	}
	return result == SE_IMAGE_OK;
}

enum
{
	SES_ROW_BANK, SES_ROW_FRAME, SES_ROW_TOOL, SES_ROW_MIRROR, SES_ROW_GUIDES, SES_ROW_SOURCE,
	SES_ROW_COUNT,
	SES_PAL_COLOR = SES_ROW_COUNT, SES_PAL_BACKGROUND,
	SES_NUDGE_LEFT, SES_NUDGE_UP, SES_NUDGE_DOWN, SES_NUDGE_RIGHT,
	SES_ACT_CAPTURE,
	SES_ACT_COPY, SES_ACT_FLIP, SES_ACT_HISTORY, SES_ACT_CLEAR, SES_ACT_BANK, SES_ACT_DONE,
	SES_NAV_COUNT,
	SES_ACT_COUNT = SES_NAV_COUNT - SES_ACT_CAPTURE,
};

enum
{
	SES_TOOL_PAINT, SES_TOOL_FILL, SES_TOOL_PICK, SES_TOOL_ERASE, SES_TOOL_SHADE, SES_TOOL_COLORIZE,
	SES_TOOL_COUNT,
};
enum { SES_GUIDES_OFF, SES_GUIDES_VERTICAL, SES_GUIDES_HORIZONTAL, SES_GUIDES_BOTH, SES_GUIDES_COUNT };
enum { SES_FLIP_H, SES_FLIP_V, SES_FOLD_L, SES_FOLD_R, SES_FOLD_U, SES_FOLD_D, SES_FLIP_COUNT };
enum { SES_BANK_IMPORT, SES_BANK_EXPORT, SES_BANK_REVERT, SES_BANK_COUNT };

static const char *const sesToolName[SES_TOOL_COUNT] = {
	"Paint", "Fill", "Pick", "Erase", "Shade", "Colorize"
};
static const char *const sesToolHelp[SES_TOOL_COUNT] = {
	"Draw with the chosen color. Right-click erases.",
	"Flood the area you touch with the chosen color.",
	"Copy the color you click, then return to Paint.",
	"Rub out pixels wherever you draw.",
	"Step a pixel lighter. Right-click steps it darker.",
	"Recolor pixels as you draw. Right-click recolors the pose.",
};
static const char *const sesGuidesName[SES_GUIDES_COUNT] = { "Off", "Vertical", "Horizontal", "Both" };
static const char *const sesFlipName[SES_FLIP_COUNT] = {
	"Flip H", "Flip V", "Fold L", "Fold R", "Fold U", "Fold D"
};

static const struct { const char *label, *help; } sesRows[SES_ROW_COUNT] = {
	{ "Custom Bank", "Choose which of your eight custom hull drawings to edit." },
	{ "Pose",        "Choose one of the five turning views." },
	{ "Tool",        "Choose what happens when you draw." },
	{ "Mirror",      "Draw on both sides of the ship at once." },
	{ "Guides",      "Show centerlines on the large drawing area." },
	{ "Copy From",   "Choose a ship or custom bank to copy artwork from." },
};

static const char *const sesPaletteHelp[] = {
	"Choose the paint color. Color 0 is transparent.",
	"Choose the preview background. It is not saved.",
};

static const char *const sesNudgeHelp[] = {
	"Move this pose left by one pixel.",
	"Move this pose up by one pixel.",
	"Move this pose down by one pixel.",
	"Move this pose right by one pixel.",
};

static const struct { const char *label, *help; } sesActs[SES_ACT_COUNT] = {
	{ "Capture All", "Copy the chosen artwork in. Left/Right picks how much." },
	{ "Copy",        "Copy the chosen pose onto this one. Left/Right chooses." },
	{ "Flip H",      "Mirror or fold this pose. Left/Right chooses." },
	{ "Undo",        "Take back or redo an edit. Also Ctrl+Z and Ctrl+Y." },
	{ "Clear",       "Erase this pose or every pose. Left/Right chooses." },
	{ "Import Image", "Load this bank's poses from a 120x28 PNG. Left/Right chooses." },
	{ "Done",        "Back to the loadout editor." },
};

static const struct { const char *label, *help; } sesBankAlt[SES_BANK_COUNT - 1] = {
	{ "Export Image", "Save this bank's poses as a 120x28 PNG. Left/Right chooses." },
	{ "Revert",       "Discard this bank's edits. Left/Right chooses." },
};

static const char *const sesPoseName[SE_FRAMES] = { "Hard Left", "Left", "Center", "Right", "Hard Right" };

static void seFloodFill(int bank, int frame, int sx, int sy, JE_byte to)
{
	const JE_byte from = *seFramePx(bank, frame, sx, sy);
	if (from == to)
		return;

	Uint16 stack[SE_FRAME_W * SE_FRAME_H * 4];
	int top = 0;
	stack[top++] = (Uint16)(sy * SE_FRAME_W + sx);
	while (top > 0)
	{
		const int p = stack[--top], x = p % SE_FRAME_W, y = p / SE_FRAME_W;
		if (*seFramePx(bank, frame, x, y) != from)
			continue;
		*seFramePx(bank, frame, x, y) = to;
		if (x > 0)
			stack[top++] = (Uint16)(p - 1);
		if (x < SE_FRAME_W - 1)
			stack[top++] = (Uint16)(p + 1);
		if (y > 0)
			stack[top++] = (Uint16)(p - SE_FRAME_W);
		if (y < SE_FRAME_H - 1)
			stack[top++] = (Uint16)(p + SE_FRAME_W);
	}
}

enum { SE_STROKE_COLOR, SE_STROKE_LIGHTEN, SE_STROKE_DARKEN, SE_STROKE_COLORIZE };

static void seColorizePx(int bank, int frame, int x, int y, JE_byte high)
{
	JE_byte *const px = seFramePx(bank, frame, x, y);
	if (*px == 0 || *px == 15)
		return;

	const JE_byte dyed = (JE_byte)(high | (*px & 0x0f));
	*px = (dyed == 0) ? 1 : dyed;
}

// One shade step per pixel per stroke; cleared where each stroke begins its undo entry.
static bool seShadeDone[SE_FRAME_H][SE_FRAME_W];

static void seShadeStrokeBegin(void)
{
	memset(seShadeDone, 0, sizeof seShadeDone);
}

static void seShadePx(int bank, int frame, int x, int y, bool darker)
{
	JE_byte *const px = seFramePx(bank, frame, x, y);
	if (*px == 0 || seShadeDone[y][x])
		return;

	seShadeDone[y][x] = true;

	const int high = *px & 0xf0, low = *px & 0x0f;
	const int minShade = (high == 0) ? 1 : 0;
	int shade = darker ? low - 1 : low + 1;
	if (shade < minShade)
		shade = minShade;
	else if (shade > 15)
		shade = 15;
	*px = (JE_byte)(high | shade);
}

static void seStrokePx(int bank, int frame, int x, int y, JE_byte c, int op)
{
	if (op == SE_STROKE_COLOR)
		*seFramePx(bank, frame, x, y) = c;
	else if (op == SE_STROKE_COLORIZE)
		seColorizePx(bank, frame, x, y, (JE_byte)(c & 0xf0));
	else
		seShadePx(bank, frame, x, y, op == SE_STROKE_DARKEN);
}

// Join pointer samples so fast strokes do not leave gaps.
static void seStrokeTo(int bank, int frame, int x0, int y0, int x1, int y1, JE_byte c, int op)
{
	const int dx = (x1 > x0) ? x1 - x0 : x0 - x1;
	const int dy = (y1 > y0) ? y0 - y1 : y1 - y0;  // negative magnitude, per Bresenham
	const int sx = (x0 < x1) ? 1 : -1;
	const int sy = (y0 < y1) ? 1 : -1;
	int err = dx + dy;

	for (;;)
	{
		seStrokePx(bank, frame, x0, y0, c, op);
		if (x0 == x1 && y0 == y1)
			break;
		const int e2 = 2 * err;
		if (e2 >= dy)
		{
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx)
		{
			err += dx;
			y0 += sy;
		}
	}
}

static void seColorizeFrame(int bank, int frame, int row)
{
	const JE_byte high = (JE_byte)((row & 0x0f) << 4);
	for (int y = 0; y < SE_FRAME_H; ++y)
		for (int x = 0; x < SE_FRAME_W; ++x)
			seColorizePx(bank, frame, x, y, high);
}

// Pick selects the color and returns to Paint. alt is right-click or Shift.
static void seApplyTool(int bank, int frame, int x, int y, int *color, int *tool, bool mirror,
                        bool alt)
{
	const int mirrorX = SE_FRAME_W - 1 - x;
	switch (*tool)
	{
	case SES_TOOL_FILL:
		seFloodFill(bank, frame, x, y, (JE_byte)*color);
		if (mirror)
			seFloodFill(bank, frame, mirrorX, y, (JE_byte)*color);
		break;
	case SES_TOOL_PICK:
		*color = *seFramePx(bank, frame, x, y);
		*tool = SES_TOOL_PAINT;
		break;
	case SES_TOOL_ERASE:
		*seFramePx(bank, frame, x, y) = 0;
		if (mirror)
			*seFramePx(bank, frame, mirrorX, y) = 0;
		break;
	case SES_TOOL_COLORIZE:
		if (alt)
			seColorizeFrame(bank, frame, *color >> 4);
		else
		{
			seColorizePx(bank, frame, x, y, (JE_byte)(*color & 0xf0));
			if (mirror)
				seColorizePx(bank, frame, mirrorX, y, (JE_byte)(*color & 0xf0));
		}
		break;
	case SES_TOOL_SHADE:
		seShadePx(bank, frame, x, y, alt);
		if (mirror)
			seShadePx(bank, frame, mirrorX, y, alt);
		break;
	default:
		*seFramePx(bank, frame, x, y) = (JE_byte)*color;
		if (mirror)
			*seFramePx(bank, frame, mirrorX, y) = (JE_byte)*color;
		break;
	}
}

static void seTouchColor(JE_byte *recent, int *count, JE_byte c)
{
	int i = 0;
	while (i < *count && recent[i] != c)
		++i;
	if (i == *count)
		++*count;
	else
		memmove(&recent[i], &recent[i + 1], (size_t)(*count - 1 - i));
	recent[*count - 1] = c;
}

static int seCycleTool(int tool, int dir, int *paletteTarget)
{
	tool = (tool + SES_TOOL_COUNT + dir) % SES_TOOL_COUNT;
	if (tool == SES_TOOL_COLORIZE)
		*paletteTarget = SES_PAL_COLOR;
	return tool;
}

static int seCycleBankMode(int mode, int dir)
{
	return (mode + SES_BANK_COUNT + dir) % SES_BANK_COUNT;
}

static int seColorStep(int tool, bool wholeRow)
{
	return (tool == SES_TOOL_COLORIZE || wholeRow) ? 16 : 1;
}

static void seTestPaintAlpha(SDL_Surface *s, int x, int y, int w, int h, SDL_Color c, Uint8 alpha)
{
	const Uint32 pixel = SDL_MapRGBA(s->format, c.r, c.g, c.b, alpha);
	SDL_FillRect(s, &(SDL_Rect){ x, y, w, h }, pixel);
}

static void seTestPaint(SDL_Surface *s, int x, int y, int w, int h, SDL_Color c)
{
	seTestPaintAlpha(s, x, y, w, h, c, SDL_ALPHA_OPAQUE);
}

static bool seTestDecodes(const Uint8 *png, size_t size, const SeStrip *want)
{
	SeStrip back;
	return size > 0 && seStripDecodePng(png, size, NULL, &back) == SE_IMAGE_OK &&
	       memcmp(&back, want, sizeof(back)) == 0;
}

static bool seTestSaves(const SeStrip *strip, const SDL_Color *colors, Uint8 *png, size_t cap,
                        const SeStrip *want)
{
	SDL_Surface *const s = seStripSurface(strip, colors);
	const size_t size = (s != NULL) ? image_png_save(s, png, cap) : 0;
	SDL_FreeSurface(s);
	return seTestDecodes(png, size, want);
}

bool JE_shipStripImageSelfTest(void)
{
	enum { MOVED = 5, BELOW = 3, ABOVE = 0x9c };

	SeUndoState saved;
	seUndoCapture(1, &saved);

	SeStrip strip, flipped, back;
	for (int y = 0; y < SE_FRAME_H; ++y)
		for (int x = 0; x < SE_STRIP_W; ++x)
		{
			strip.px[y][x] = (JE_byte)(y * SE_STRIP_W + x);
			flipped.px[y][x] = (JE_byte)(255 - strip.px[y][x]);
		}
	seStripApply(1, &strip);

	const char *const importHelp = sesActs[SES_ACT_BANK - SES_ACT_CAPTURE].help;
	char dims[16];
	snprintf(dims, sizeof(dims), "%dx%d", SE_STRIP_W, SE_FRAME_H);
	Uint8 png[SE_STRIP_PNG_MAX_BYTES];
	SeStrip captured;
	seStripCapture(1, &captured);
	const size_t exported = seStripEncodePng(&captured, png, sizeof(png));
	bool ok = strstr(importHelp, dims) != NULL && memcmp(&captured, &strip, sizeof(strip)) == 0 &&
	          seTestDecodes(png, exported, &strip);

	SDL_Surface *const decoded = ok ? image_png_load(png, exported) : NULL;
	SDL_Surface *const truecolor =
		(decoded != NULL) ? SDL_ConvertSurfaceFormat(decoded, SDL_PIXELFORMAT_ARGB8888, 0) : NULL;
	ok = truecolor != NULL && seStripFromSurface(truecolor, NULL, &back) &&
	     memcmp(&back, &strip, sizeof(strip)) == 0;
	SDL_FreeSurface(truecolor);
	SDL_FreeSurface(decoded);

	SDL_Color image[256], reversed[256], respun[256], legacy[256];
	seImagePalette(image);
	image[0].a = SDL_ALPHA_TRANSPARENT;
	for (int i = 0; i < 256; ++i)
	{
		reversed[i] = image[255 - i];
		respun[i] = image[(i == 0) ? MOVED : (i <= MOVED) ? i - 1 : i];
		respun[i].a = (i == 0) ? SDL_ALPHA_TRANSPARENT : SDL_ALPHA_OPAQUE;
		legacy[i] = palettes[0][i];
		legacy[i].a = SDL_ALPHA_OPAQUE;
	}
	ok = ok && seTestSaves(&flipped, reversed, png, sizeof(png), &strip) &&
	     seTestSaves(&strip, legacy, png, sizeof(png), &strip);

	SeStrip spun, want;
	memset(&spun, 0, sizeof(spun));
	memset(&want, 0, sizeof(want));
	spun.px[0][0] = BELOW + 1;
	spun.px[0][1] = want.px[0][1] = ABOVE;
	spun.px[0][2] = 1;
	want.px[0][0] = BELOW;
	ok = ok && seTestSaves(&spun, respun, png, sizeof(png), &want);

	SDL_Surface *const old = ok ? seStripSurface(&strip, legacy) : NULL;
	SDL_Surface *const rgb =
		(old != NULL) ? SDL_ConvertSurfaceFormat(old, SDL_PIXELFORMAT_RGB24, 0) : NULL;
	ok = rgb != NULL && seStripFromSurface(rgb, &strip, &back) &&
	     memcmp(&back, &strip, sizeof(strip)) == 0 && seStripFromSurface(rgb, NULL, &back);
	for (int y = 0; y < SE_FRAME_H && ok; ++y)
		for (int x = 0; x < SE_STRIP_W && ok; ++x)
			ok = seSameRgb(palettes[0][back.px[y][x]], palettes[0][strip.px[y][x]]) &&
			     (back.px[y][x] == 0) == (strip.px[y][x] == 0);
	SDL_FreeSurface(rgb);
	SDL_FreeSurface(old);

	SDL_Surface *const argb = ok ? seStripCanvas(SDL_PIXELFORMAT_ARGB8888) : NULL;
	ok = argb != NULL;
	if (ok)
	{
		SDL_FillRect(argb, NULL, SDL_MapRGBA(argb->format, 255, 255, 255, SDL_ALPHA_TRANSPARENT));
		seTestPaint(argb, 1, 0, 1, 1, palettes[0][ABOVE]);
		seTestPaint(argb, 2, 0, 1, 1, palettes[0][0]);
		seTestPaintAlpha(argb, 3, 0, 1, 1, palettes[0][ABOVE], SDL_ALPHA_OPAQUE / 2 - 1);
		seTestPaintAlpha(argb, 4, 0, 1, 1, palettes[0][ABOVE], SDL_ALPHA_OPAQUE / 2);
		ok = seStripFromSurface(argb, NULL, &back) &&
		     back.px[0][0] == 0 && back.px[0][1] == ABOVE && back.px[0][2] == 0 &&
		     back.px[0][3] == 0 && back.px[0][4] == ABOVE;
		SDL_FreeSurface(argb);
	}

	SDL_Surface *const pose = ok ? SDL_CreateRGBSurfaceWithFormat(0, SE_FRAME_W, SE_FRAME_H, 8,
	                                                              SDL_PIXELFORMAT_INDEX8) : NULL;
	ok = pose != NULL;
	if (ok)
	{
		const size_t size = image_png_save(pose, png, sizeof(png));
		ok = size > 0 && seStripDecodePng(png, size, NULL, &back) == SE_IMAGE_WRONG_SIZE;
		SDL_FreeSurface(pose);
	}

	if (ok)
	{
		memset(png, 0xab, 64);
		ok = seStripDecodePng(png, 64, NULL, &back) == SE_IMAGE_UNREADABLE;
	}

	ok = ok && JE_textWidth(importHelp, small_font) < LEGACY_WIDTH;
	for (int i = 0; i < SES_BANK_COUNT - 1; ++i)
		ok = ok && JE_textWidth(sesBankAlt[i].help, small_font) < LEGACY_WIDTH;

	seUndoRestore(&saved);
	return ok;
}

static int seBankUniqueInk(int bank)
{
	for (int i = SDL_max(bank * 16, 1); i < bank * 16 + 16; ++i)
		if (!seInkShared((JE_byte)i))
			return i;
	return 0;
}

static SDL_Color seOffPalette(SDL_Color c)
{
	c.g = (Uint8)((c.g < 255) ? c.g + 1 : c.g - 1);
	c.b = (Uint8)((c.b < 255) ? c.b + 1 : c.b - 1);
	return c;
}

static SDL_Point seTestSlot(int slot)
{
	enum { SLOT = 4, ACROSS = SE_STRIP_W / SLOT };
	return (SDL_Point){ slot % ACROSS * SLOT + 1, slot / ACROSS * SLOT + 1 };
}

bool JE_shipStripColorSelfTest(void)
{
	enum { SLOTS = (SE_STRIP_W / 4) * (SE_FRAME_H / 4), HULL = 0x7a, HIGHLIGHT = 0x7f };

	SDL_Color image[256];
	seImagePalette(image);
	bool ok = true;
	for (int i = 1; i < 256 && ok; ++i)
	{
		ok = seInkShared((JE_byte)i)
		   ? seImageColorFree(image[i], image, 0) && palette_distance(image[i], palettes[0][i]) <= 4
		   : seSameRgb(image[i], palettes[0][i]);
		for (int j = 0; j < i && ok; ++j)
			ok = !seSameRgb(image[i], image[j]);
	}

	SDL_Surface *const s = seStripCanvas(SDL_PIXELFORMAT_RGB24);
	ok = ok && s != NULL;
	SeStrip back;
	for (int pass = 0; pass < 4 && ok; ++pass)
	{
		const int first = 1 + (pass % 2) * SLOTS, last = SDL_min(first + SLOTS, 256);
		SDL_FillRect(s, NULL, 0);
		for (int i = first; i < last; ++i)
		{
			const SDL_Point at = seTestSlot(i - first);
			const int unique = seBankUniqueInk(i >> 4);
			const SDL_Color ink = (pass < 2) ? palettes[0][i] : seOffPalette(palettes[0][i]);
			if (unique != 0)
				seTestPaint(s, at.x - 1, at.y - 1, 3, 3, palettes[0][unique]);
			seTestPaint(s, at.x, at.y, 1, 1, ink);
		}
		ok = seStripFromSurface(s, NULL, &back);
		for (int i = first; i < last && ok; ++i)
		{
			const SDL_Point at = seTestSlot(i - first);
			const JE_byte got = back.px[at.y][at.x];
			ok = (seBankUniqueInk(i >> 4) != 0) ? got == i
			                                    : seSameRgb(palettes[0][got], palettes[0][i]);
		}
	}

	if (ok)
	{
		SDL_FillRect(s, NULL, 0);
		seTestPaint(s, 4, 4, 3, 3, palettes[0][HULL]);
		seTestPaint(s, 60, 20, 1, 1, palettes[0][HIGHLIGHT]);
		ok = !seInkShared(HULL) && seInkShared(HIGHLIGHT) && seStripFromSurface(s, NULL, &back) &&
		     back.px[20][60] == HIGHLIGHT;

		SDL_FillRect(s, NULL, 0);
		seTestPaint(s, 60, 20, 1, 1, palettes[0][HIGHLIGHT]);
		ok = ok && seStripFromSurface(s, NULL, &back) && back.px[20][60] < HIGHLIGHT &&
		     seSameRgb(palettes[0][back.px[20][60]], palettes[0][HIGHLIGHT]);
	}
	SDL_FreeSurface(s);
	return ok;
}

static void seSpriteEditor(int bank)
{
	enum { BOX_X0 = 8, BOX_Y0 = 8, BOX_X1 = 143, BOX_Y1 = 182 };
	enum { CANV_X = 28, CANV_Y = 22, CANV_SCALE = 4 };
	enum { STRIP_X = 12, STRIP_Y = 151 };
	enum { PAL_X = 199, PAL_Y = 75, PAL_CELL = 4 };
	enum { PAL_BTN_Y = 81, PAL_BTN_SIZE = 32, PAL_COL_X = 160, PAL_BG_X = 272 };
	enum { NUDGE_BTN_Y = 117, NUDGE_BTN_SIZE = 15 };
	enum { USED_X = 11, USED_Y = 139, USED_CELL = 5, USED_MAX = 26 };
	const int panX0 = 150, panX1 = 313, panY0 = 7, panY1 = 183;
	const int fieldsTop = panY0 + 13;
	const int row_h = 9;
	const int actionsTop = 144;
	const int act_h = 10;
	const int panMidX = (panX0 + panX1) / 2;
	const int labelX = panX0 + 5, valueX = panX1 - 5;

	JE_byte captureHulls[SHIP_DRAGONWING];
	const int captureHullCount =
	    JE_distinctShipHulls(captureHulls, COUNTOF(captureHulls), seCaptureHullOk);
	const int sourceCount = captureHullCount + SE_BANKS - 1;
	int frame = 3, copyPose = 3, color = 15, source = 0, tool = SES_TOOL_PAINT;
	static int background = C_PANEL;
	static int guides = SES_GUIDES_OFF;
	bool mirror = false;
	bool clearAll = false;
	bool captureAll = true;
	int flipMode = SES_FLIP_H;
	bool redoMode = false;
	int bankMode = SES_BANK_IMPORT;
	seUndoBase = seUndoCount = seRedoCount = 0;
	int selected = SES_ROW_BANK;
	int paletteTarget = SES_PAL_COLOR;
	const int nudgeX[] = { PAL_COL_X - 1, PAL_COL_X + 18, PAL_BG_X - 1, PAL_BG_X + 18 };
	const int nudgeDx[] = { -1, 0, 0, 1 };
	const int nudgeDy[] = { 0, -1, 1, 0 };
	bool canvasFocus = false;
	int curX = SE_FRAME_W / 2, curY = SE_FRAME_H / 2;
	JE_byte heldColor = 0;  // the color the pressed mouse button paints while dragging
	bool heldDarken = false;
	int strokeX = -1, strokeY = -1;  // last cell this drag painted; -1 = no stroke in progress
	JE_byte recent[256];
	int recentCount = 0;
	int lastX = -1, lastY = -1;
	char notice[64] = "";
	int prev_mx = mouse_x, prev_my = mouse_y;
	Uint32 starClock = 0;
	Uint32 starLast = SDL_GetTicks();
	bool done = false;

	wait_noinput_consume(false, false, true);
	mouseShiftKeepsCursor = true;
	mouseTwoFingerRightClick = true;

	const EditorMusicTick musicTick = seMusicTick();
	while (!done)
	{
		setDelay(EDITOR_TICK_DELAY);
		editorServiceNetwork();
		if (musicTick != NULL)
			musicTick();

		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		fill_rectangle_xy(VGAScreen, BOX_X0, BOX_Y0, BOX_X1, BOX_Y1, SE_BG_SPACE);
		JE_rectangle(VGAScreen, BOX_X0 - 1, BOX_Y0 - 1, BOX_X1 + 1, BOX_Y1 + 1, C_HI);

		char caption[24];
		snprintf(caption, sizeof(caption), "Custom Bank %d", bank);
		draw_font_hv_shadow(VGAScreen, (BOX_X0 + BOX_X1) / 2, BOX_Y0 + 4, caption, small_font, centered, 5, 4, true, 1);

		fill_rectangle_wh(VGAScreen, CANV_X, CANV_Y, SE_FRAME_W * CANV_SCALE,
		                  SE_FRAME_H * CANV_SCALE, (Uint8)background);
		for (int y = 0; y < SE_FRAME_H; ++y)
			for (int x = 0; x < SE_FRAME_W; ++x)
			{
				const JE_byte c = *seFramePx(bank, frame, x, y);
				if (c != 0)
					fill_rectangle_wh(VGAScreen, CANV_X + x * CANV_SCALE, CANV_Y + y * CANV_SCALE,
					                  CANV_SCALE, CANV_SCALE, c);
			}
		const int centerX = CANV_X + SE_FRAME_W * CANV_SCALE / 2;
		const int centerY = CANV_Y + SE_FRAME_H * CANV_SCALE / 2;
		if (guides == SES_GUIDES_VERTICAL || guides == SES_GUIDES_BOTH)
		{
			for (int y = centerY - 1; y >= CANV_Y - 1; y -= CANV_SCALE)
				fill_rectangle_wh(VGAScreen, centerX - 1, y, 2, 2, C_GUIDE);
			for (int y = centerY - 1 + CANV_SCALE; y < CANV_Y + SE_FRAME_H * CANV_SCALE; y += CANV_SCALE)
				fill_rectangle_wh(VGAScreen, centerX - 1, y, 2, 2, C_GUIDE);
		}
		if (guides == SES_GUIDES_HORIZONTAL || guides == SES_GUIDES_BOTH)
		{
			for (int x = centerX - 1; x >= CANV_X - 1; x -= CANV_SCALE)
				fill_rectangle_wh(VGAScreen, x, centerY - 1, 2, 2, C_GUIDE);
			for (int x = centerX - 1 + CANV_SCALE; x < CANV_X + SE_FRAME_W * CANV_SCALE; x += CANV_SCALE)
				fill_rectangle_wh(VGAScreen, x, centerY - 1, 2, 2, C_GUIDE);
		}
		JE_rectangle(VGAScreen, CANV_X - 1, CANV_Y - 1,
		             CANV_X + SE_FRAME_W * CANV_SCALE, CANV_Y + SE_FRAME_H * CANV_SCALE, SE_BG_CLAMP);
		if (canvasFocus)
			JE_rectangle(VGAScreen, CANV_X + curX * CANV_SCALE - 1, CANV_Y + curY * CANV_SCALE - 1,
			             CANV_X + curX * CANV_SCALE + CANV_SCALE, CANV_Y + curY * CANV_SCALE + CANV_SCALE, C_HI);

		bool colorUsed[256] = { false };
		for (int y = 0; y < SE_FRAME_H; ++y)
			for (int x = 0; x < SE_FRAME_W; ++x)
				colorUsed[*seFramePx(bank, frame, x, y)] = true;
		colorUsed[0] = false;

		bool recentSeen[256] = { false };
		for (int i = 0; i < recentCount; ++i)
			recentSeen[recent[i]] = true;

		JE_byte order[256];
		int orderCount = 0;
		for (int c = 1; c < 256; ++c)
			if (colorUsed[c] && !recentSeen[c])
				order[orderCount++] = (JE_byte)c;
		for (int i = 0; i < recentCount; ++i)
			if (colorUsed[recent[i]])
				order[orderCount++] = recent[i];

		const int usedCount = (orderCount < USED_MAX) ? orderCount : USED_MAX;
		const JE_byte *const usedList = order + orderCount - usedCount;
		const int usedW = usedCount * USED_CELL;
		const int usedX0 = USED_X + (USED_MAX * USED_CELL - usedW) / 2;

		int usedSel = -1;
		if (usedCount > 0)
		{
			for (int i = 0; i < usedCount; ++i)
			{
				fill_rectangle_wh(VGAScreen, usedX0 + i * USED_CELL, USED_Y,
				                  USED_CELL, USED_CELL, usedList[i]);
				if (usedList[i] == color)
					usedSel = i;
			}
			JE_rectangle(VGAScreen, usedX0 - 1, USED_Y - 1,
			             usedX0 + usedW, USED_Y + USED_CELL, SE_BG_CLAMP);
		}
		if (usedSel >= 0)
			JE_rectangle(VGAScreen, usedX0 + usedSel * USED_CELL - 1, USED_Y - 1,
			             usedX0 + usedSel * USED_CELL + USED_CELL, USED_Y + USED_CELL, SE_BG_SELECT);

		fill_rectangle_xy(VGAScreen, BOX_X0, STRIP_Y - 3, BOX_X1, STRIP_Y + SE_FRAME_H + 2, SE_BG_BAND);
		fill_rectangle_xy(VGAScreen, BOX_X0, STRIP_Y - 3, BOX_X1, STRIP_Y - 3, SE_BG_RIM);
		fill_rectangle_xy(VGAScreen, BOX_X0, STRIP_Y + SE_FRAME_H + 2, BOX_X1,
		                  STRIP_Y + SE_FRAME_H + 2, SE_BG_RIM);

		for (int f = 0; f < SE_FRAMES; ++f)
		{
			fill_rectangle_wh(VGAScreen, STRIP_X + f * 26, STRIP_Y,
			                  SE_FRAME_W, SE_FRAME_H, (Uint8)background);
			seDrawFramePx(STRIP_X + f * 26, STRIP_Y, bank, f + 1);
			if (f + 1 == frame)
				draw_corner_clamps(VGAScreen, STRIP_X + f * 26 - 1, STRIP_Y - 1,
				                   STRIP_X + f * 26 + SE_FRAME_W, STRIP_Y + SE_FRAME_H, 4,
				                   SE_BG_SELECT);
		}

		fill_rectangle_xy(VGAScreen, panX0, panY0, panX1, panY1, C_PANEL);
		JE_rectangle(VGAScreen, panX0, panY0, panX1, panY1, C_HI);
		draw_font_hv_shadow(VGAScreen, panMidX, panY0 + 2, "SPRITE EDITOR", small_font,
		                    centered, 15, 3, false, 1);
		fill_rectangle_xy(VGAScreen, panX0 + 2, panY0 + 10, panX1 - 2, panY0 + 10, C_DIV);

		for (int r = 0; r < SES_ROW_COUNT; ++r)
		{
			const int ry = fieldsTop + r * row_h;
			const bool sel = (selected == r && !canvasFocus);
			fill_rectangle_xy(VGAScreen, panX0 + 2, ry - 1, panX1 - 2, ry + row_h - 3, sel ? C_SEL : C_PANEL);

			char raw[40], val[48];
			switch (r)
			{
			case SES_ROW_BANK:   snprintf(raw, sizeof(raw), "%d of 8", bank); break;
			case SES_ROW_FRAME:  snprintf(raw, sizeof(raw), "%s", sesPoseName[frame - 1]); break;
			case SES_ROW_TOOL:   snprintf(raw, sizeof(raw), "%s", sesToolName[tool]); break;
			case SES_ROW_MIRROR: snprintf(raw, sizeof(raw), "%s", mirror ? "On" : "Off"); break;
			case SES_ROW_GUIDES: snprintf(raw, sizeof(raw), "%s", sesGuidesName[guides]); break;
			default:
				if (source >= captureHullCount)
					snprintf(raw, sizeof(raw), "Custom %d",
					         seCustomSourceBank(source - captureHullCount, bank));
				else if (captureHullCount > 0)
					JE_shipShortName(captureHulls[source], raw, sizeof(raw));
				else
					SDL_strlcpy(raw, "None", sizeof(raw));
				break;
			}
			if (sel)
				snprintf(val, sizeof(val), "< %s >", raw);
			else
				SDL_strlcpy(val, raw, sizeof(val));
			if (labelX + JE_textWidth(sesRows[r].label, small_font) + 6 <= valueX - JE_textWidth(val, small_font))
				draw_font_hv_shadow(VGAScreen, labelX, ry, sesRows[r].label, small_font, left_aligned, 15, sel ? 5 : 3, false, 1);
			draw_font_hv_shadow(VGAScreen, valueX, ry, val, small_font, right_aligned, 15, sel ? 6 : 5, false, 1);
		}

		for (int c = 0; c < 256; ++c)
			fill_rectangle_wh(VGAScreen, PAL_X + (c % 16) * PAL_CELL, PAL_Y + (c / 16) * PAL_CELL,
			                  PAL_CELL, PAL_CELL, (Uint8)c);
		JE_rectangle(VGAScreen, PAL_X - 1, PAL_Y - 1, PAL_X + 16 * PAL_CELL, PAL_Y + 16 * PAL_CELL, C_DIV);

		const int paletteButtons[] = { PAL_COL_X, PAL_BG_X };
		const int paletteColors[] = { color, background };
		const char *const paletteLabels[] = { "Col", "BG" };
		for (int b = 0; b < 2; ++b)
		{
			const bool sel = (selected == SES_PAL_COLOR + b && !canvasFocus);
			const int bx = paletteButtons[b];
			fill_rectangle_wh(VGAScreen, bx, PAL_BTN_Y, PAL_BTN_SIZE, PAL_BTN_SIZE, sel ? C_SEL : C_PANEL);
			JE_rectangle(VGAScreen, bx - 1, PAL_BTN_Y - 1,
			             bx + PAL_BTN_SIZE, PAL_BTN_Y + PAL_BTN_SIZE, sel ? C_HI : C_DIV);
			draw_font_hv_shadow(VGAScreen, bx + PAL_BTN_SIZE / 2, PAL_BTN_Y + 4,
			                    paletteLabels[b], small_font, centered, 15, sel ? 6 : 4, false, 1);
			const int swX0 = bx + 6, swX1 = bx + PAL_BTN_SIZE - 7;
			if (b == 0 && tool == SES_TOOL_COLORIZE)
			{
				const int swW = swX1 - swX0 + 1;
				for (int x = swX0; x <= swX1; ++x)
					fill_rectangle_xy(VGAScreen, x, PAL_BTN_Y + 17, x, PAL_BTN_Y + PAL_BTN_SIZE - 6,
					                  (Uint8)((color & 0xf0) | ((x - swX0) * 16 / swW)));
			}
			else
			{
				fill_rectangle_xy(VGAScreen, swX0, PAL_BTN_Y + 17, swX1,
				                  PAL_BTN_Y + PAL_BTN_SIZE - 6, (Uint8)paletteColors[b]);
			}
			JE_rectangle(VGAScreen, bx + 5, PAL_BTN_Y + 16,
			             bx + PAL_BTN_SIZE - 6, PAL_BTN_Y + PAL_BTN_SIZE - 5, C_DIV);
		}
		for (int n = 0; n < 4; ++n)
		{
			const bool sel = (selected == SES_NUDGE_LEFT + n && !canvasFocus);
			fill_rectangle_wh(VGAScreen, nudgeX[n], NUDGE_BTN_Y,
			                  NUDGE_BTN_SIZE, NUDGE_BTN_SIZE, sel ? C_SEL : C_PANEL);
			JE_rectangle(VGAScreen, nudgeX[n] - 1, NUDGE_BTN_Y - 1,
			             nudgeX[n] + NUDGE_BTN_SIZE, NUDGE_BTN_Y + NUDGE_BTN_SIZE,
			             sel ? C_HI : C_DIV);
			seDrawArrow(nudgeX[n] + 4, NUDGE_BTN_Y + 4, n, C_HI);
		}

		const int paletteSelection = (paletteTarget == SES_PAL_BACKGROUND) ? background : color;
		const bool dyeRow = (tool == SES_TOOL_COLORIZE && paletteTarget != SES_PAL_BACKGROUND);
		const int selX0 = dyeRow ? PAL_X - 1 : PAL_X + (paletteSelection % 16) * PAL_CELL - 1;
		const int selX1 = dyeRow ? PAL_X + 16 * PAL_CELL
		                         : PAL_X + (paletteSelection % 16) * PAL_CELL + PAL_CELL;
		JE_rectangle(VGAScreen, selX0, PAL_Y + (paletteSelection / 16) * PAL_CELL - 1,
		             selX1, PAL_Y + (paletteSelection / 16) * PAL_CELL + PAL_CELL, C_HI);

		fill_rectangle_xy(VGAScreen, panX0 + 2, actionsTop - 3, panX1 - 2, actionsTop - 3, C_DIV);
		for (int a = 0; a < SES_ACT_COUNT; ++a)
		{
			const bool alone = (a == SES_ACT_COUNT - 1);
			const int bx0 = (alone || a % 2 == 0) ? panX0 + 2 : panMidX + 1;
			const int bx1 = (alone || a % 2 == 1) ? panX1 - 2 : panMidX - 1;
			const int ry = actionsTop + (a / 2) * act_h;
			const bool sel = (selected == SES_ACT_CAPTURE + a && !canvasFocus);
			fill_rectangle_xy(VGAScreen, bx0, ry - 1, bx1, ry + act_h - 3, sel ? C_SEL : C_DIV);
			const int id = SES_ACT_CAPTURE + a;
			char copyLabel[24];
			const char *label = sesActs[a].label;
			if (id == SES_ACT_COPY)
			{
				snprintf(copyLabel, sizeof(copyLabel), "%s %s", label, sesPoseName[copyPose - 1]);
				label = copyLabel;
			}
			else if (id == SES_ACT_CAPTURE && !captureAll)
				label = "Capture Pose";
			else if (id == SES_ACT_FLIP)
				label = sesFlipName[flipMode];
			else if (id == SES_ACT_HISTORY && redoMode)
				label = "Redo";
			else if (id == SES_ACT_CLEAR && clearAll)
				label = "Clear All";
			else if (id == SES_ACT_BANK && bankMode != SES_BANK_IMPORT)
				label = sesBankAlt[bankMode - 1].label;
			if (id >= SES_ACT_CAPTURE && id <= SES_ACT_BANK)
			{
				const Uint8 dot = (Uint8)((sel ? C_SEL : C_DIV) - 2);
				fill_rectangle_wh(VGAScreen, bx0 + 1, ry + 2, 2, 2, dot);
				fill_rectangle_wh(VGAScreen, bx1 - 2, ry + 2, 2, 2, dot);
			}
			draw_font_hv_shadow(VGAScreen, (bx0 + bx1) / 2, ry, label, small_font, centered,
			                    15, sel ? 6 : 4, false, 1);
		}

		const char *const help = canvasFocus ? "Arrows move, Enter uses the tool, Backspace erases, Tab leaves."
		                       : selected == SES_ROW_TOOL ? sesToolHelp[tool]
		                       : selected < SES_ROW_COUNT ? sesRows[selected].help
		                       : selected == SES_PAL_COLOR && tool == SES_TOOL_COLORIZE
		                         ? "Choose the dye color. Enter here recolors the whole pose."
		                       : selected < SES_NUDGE_LEFT ? sesPaletteHelp[selected - SES_PAL_COLOR]
		                       : selected < SES_ACT_CAPTURE ? sesNudgeHelp[selected - SES_NUDGE_LEFT]
		                       : selected == SES_ACT_BANK && bankMode != SES_BANK_IMPORT
		                         ? sesBankAlt[bankMode - 1].help
		                       : sesActs[selected - SES_ACT_CAPTURE].help;
		editorDrawHint(notice[0] != '\0' ? notice : help);

		touch_ui_set_layout(TOUCH_LAYOUT_LIST);

		push_joysticks_as_keyboard();
		service_SDL_events(false);

		mouseCursor = MOUSE_POINTER_NORMAL;
		sePresentPreviewTick(&starClock, &starLast, 0, 0);

		const bool mouseMoved = (mouse_x != prev_mx || mouse_y != prev_my);

		int act = -1;  // a triggered tool button, performed after input decoding
		bool dyeNow = false;
		bool paintedColor = false;

		if (mouse_scroll != 0)
		{
			selected -= mouse_scroll;
			selected = selected < 0 ? 0 : (selected >= SES_NAV_COUNT ? SES_NAV_COUNT - 1 : selected);
			if (selected == SES_PAL_COLOR || selected == SES_PAL_BACKGROUND)
				paletteTarget = selected;
			canvasFocus = false;
			mouse_scroll = 0;
		}

		const bool overCanvas = mouse_x >= CANV_X && mouse_x < CANV_X + SE_FRAME_W * CANV_SCALE &&
		                        mouse_y >= CANV_Y && mouse_y < CANV_Y + SE_FRAME_H * CANV_SCALE;

		// Do not join strokes across a release or the canvas edge.
		if (!mousedown || !overCanvas)
			strokeX = strokeY = -1;

		int hover = -1;
		for (int n = 0; n < 4; ++n)
			if (mouse_y >= NUDGE_BTN_Y && mouse_y < NUDGE_BTN_Y + NUDGE_BTN_SIZE &&
			    mouse_x >= nudgeX[n] && mouse_x < nudgeX[n] + NUDGE_BTN_SIZE)
				hover = SES_NUDGE_LEFT + n;
		if (hover < 0 && mouse_y >= PAL_BTN_Y && mouse_y < PAL_BTN_Y + PAL_BTN_SIZE &&
		    mouse_x >= PAL_COL_X && mouse_x < PAL_COL_X + PAL_BTN_SIZE)
			hover = SES_PAL_COLOR;
		else if (hover < 0 && mouse_y >= PAL_BTN_Y && mouse_y < PAL_BTN_Y + PAL_BTN_SIZE &&
		         mouse_x >= PAL_BG_X && mouse_x < PAL_BG_X + PAL_BTN_SIZE)
			hover = SES_PAL_BACKGROUND;
		else if (hover < 0 && mouse_x >= panX0 && mouse_x <= panX1)
		{
			if (mouse_y >= fieldsTop - 1 &&
			    mouse_y < fieldsTop - 1 + SES_ROW_COUNT * row_h)
				hover = (mouse_y - (fieldsTop - 1)) / row_h;
			else if (mouse_y >= actionsTop - 1 && mouse_y < actionsTop + ((SES_ACT_COUNT + 1) / 2) * act_h)
			{
				int a = ((mouse_y - (actionsTop - 1)) / act_h) * 2;
				if (a + 1 < SES_ACT_COUNT && mouse_x >= panMidX)
					a += 1;
				if (a >= SES_ACT_COUNT)
					a = SES_ACT_COUNT - 1;
				hover = SES_ACT_CAPTURE + a;
			}
		}
		if (hover >= 0 && (mouse_x != prev_mx || mouse_y != prev_my) && hover != selected)
		{
			JE_playSampleNum(S_CURSOR);
			selected = hover;
			if (selected == SES_PAL_COLOR || selected == SES_PAL_BACKGROUND)
				paletteTarget = selected;
		}
		prev_mx = mouse_x;
		prev_my = mouse_y;

		if (newmouse || (mousedown && mouseMoved))
		{
			if (newmouse)
			{
				notice[0] = '\0';
				heldColor = (lastmouse_but == SDL_BUTTON_RIGHT || tool == SES_TOOL_ERASE) ? 0 : (JE_byte)color;
				heldDarken = (lastmouse_but == SDL_BUTTON_RIGHT);
			}

			if (overCanvas)
			{
				const int px = (mouse_x - CANV_X) / CANV_SCALE;
				const int py = (mouse_y - CANV_Y) / CANV_SCALE;
				const bool pressEdit = newmouse && lastmouse_but != SDL_BUTTON_MIDDLE &&
				                       (lastmouse_but == SDL_BUTTON_RIGHT || tool != SES_TOOL_PICK);
				// Right-held Colorize is not a stroke: the press already dyed the whole pose.
				const bool freehand = (tool == SES_TOOL_PAINT || tool == SES_TOOL_ERASE ||
				                       tool == SES_TOOL_SHADE ||
				                       (tool == SES_TOOL_COLORIZE && !heldDarken));
				const bool strokeStart = !newmouse && strokeX < 0 && freehand;
				if (pressEdit || strokeStart)
				{
					seUndoBegin(bank);
					seShadeStrokeBegin();
				}
				const int op = (tool == SES_TOOL_SHADE)
				             ? (heldDarken ? SE_STROKE_DARKEN : SE_STROKE_LIGHTEN)
				             : (tool == SES_TOOL_COLORIZE) ? SE_STROKE_COLORIZE : SE_STROKE_COLOR;
				const bool shiftLine = newmouse && freehand && lastX >= 0 &&
				                       lastmouse_but != SDL_BUTTON_MIDDLE &&
				                       (keysactive[SDL_SCANCODE_LSHIFT] != 0 ||
				                        keysactive[SDL_SCANCODE_RSHIFT] != 0);
				if (newmouse && lastmouse_but == SDL_BUTTON_MIDDLE)
					color = *seFramePx(bank, frame, px, py);
				else if (shiftLine)
				{
					paintedColor = (op == SE_STROKE_COLOR && heldColor != 0);
					seStrokeTo(bank, frame, lastX, lastY, px, py, heldColor, op);
					if (mirror)
						seStrokeTo(bank, frame, SE_FRAME_W - 1 - lastX, lastY,
						           SE_FRAME_W - 1 - px, py, heldColor, op);
				}
				else if (newmouse && lastmouse_but == SDL_BUTTON_RIGHT &&
				         tool != SES_TOOL_SHADE && tool != SES_TOOL_COLORIZE)
				{
					*seFramePx(bank, frame, px, py) = 0;
					if (mirror)
						*seFramePx(bank, frame, SE_FRAME_W - 1 - px, py) = 0;
				}
				else if (newmouse)
				{
					paintedColor = (tool == SES_TOOL_PAINT || tool == SES_TOOL_FILL);
					seApplyTool(bank, frame, px, py, &color, &tool, mirror, heldDarken);
				}
				else if (freehand && (px != strokeX || py != strokeY))
				{
					paintedColor = (op == SE_STROKE_COLOR && heldColor != 0);
					if (strokeX >= 0)
					{
						seStrokeTo(bank, frame, strokeX, strokeY, px, py, heldColor, op);
						if (mirror)
							seStrokeTo(bank, frame, SE_FRAME_W - 1 - strokeX, strokeY,
							           SE_FRAME_W - 1 - px, py, heldColor, op);
					}
					else
					{
						seStrokePx(bank, frame, px, py, heldColor, op);
						if (mirror)
							seStrokePx(bank, frame, SE_FRAME_W - 1 - px, py, heldColor, op);
					}
				}
				strokeX = px;
				strokeY = py;
				if (freehand)
				{
					lastX = px;
					lastY = py;
				}
			}
			else if (newmouse)
			{
				int pick = -1;
				if (mouse_x >= PAL_X && mouse_x < PAL_X + 16 * PAL_CELL &&
				    mouse_y >= PAL_Y && mouse_y < PAL_Y + 16 * PAL_CELL)
					pick = (mouse_y - PAL_Y) / PAL_CELL * 16 + (mouse_x - PAL_X) / PAL_CELL;
				else if (mouse_y >= USED_Y && mouse_y < USED_Y + USED_CELL &&
				         mouse_x >= usedX0 && mouse_x < usedX0 + usedCount * USED_CELL)
					pick = usedList[(mouse_x - usedX0) / USED_CELL];

				if (pick >= 0)
				{
					if (paletteTarget == SES_PAL_BACKGROUND)
						background = pick;
					else
						color = pick;
					JE_playSampleNum(S_CURSOR);
				}
				else if (mouse_y >= STRIP_Y && mouse_y < STRIP_Y + SE_FRAME_H &&
				         mouse_x >= STRIP_X && mouse_x < STRIP_X + SE_FRAMES * 26)
				{
					frame = (mouse_x - STRIP_X) / 26 + 1;
					editorFlashStart(&seFlash, STRIP_X + (frame - 1) * 26, STRIP_Y,
					                 STRIP_X + (frame - 1) * 26 + SE_FRAME_W - 1,
					                 STRIP_Y + SE_FRAME_H - 1);
					JE_playSampleNum(S_CURSOR);
				}
				else if (hover >= 0)
				{
					selected = hover;
					if (selected == SES_PAL_COLOR || selected == SES_PAL_BACKGROUND)
						paletteTarget = selected;
					canvasFocus = false;
					if (hover < SES_ROW_COUNT)
					{
						const int click = (lastmouse_but == SDL_BUTTON_RIGHT) ? -1 : 1;
						const int dir = (mouse_x < panMidX) ? -click : click;
						switch (hover)
						{
						case SES_ROW_BANK:   bank = (bank + 7 + dir) % 8 + 1; break;
						case SES_ROW_FRAME:  frame = (frame + 4 + dir) % 5 + 1; break;
						case SES_ROW_TOOL:   tool = seCycleTool(tool, dir, &paletteTarget); break;
						case SES_ROW_MIRROR: mirror = !mirror; break;
						case SES_ROW_GUIDES: guides = (guides + SES_GUIDES_COUNT + dir) % SES_GUIDES_COUNT; break;
						default:
							if (sourceCount > 0)
								source = (source + sourceCount + dir) % sourceCount;
							break;
						}
						JE_playSampleNum(S_CURSOR);
					}
					else if (hover == SES_PAL_COLOR || hover == SES_PAL_BACKGROUND)
						JE_playSampleNum(S_CURSOR);
					else if (lastmouse_but == SDL_BUTTON_RIGHT)
					{
						switch (hover)
						{
						case SES_ACT_CAPTURE: captureAll = !captureAll; break;
						case SES_ACT_COPY:    copyPose = copyPose % 5 + 1; break;
						case SES_ACT_FLIP:    flipMode = (flipMode + 1) % SES_FLIP_COUNT; break;
						case SES_ACT_HISTORY: redoMode = !redoMode; break;
						case SES_ACT_CLEAR:   clearAll = !clearAll; break;
						case SES_ACT_BANK:    bankMode = seCycleBankMode(bankMode, 1); break;
						default:              break;
						}
						if (hover >= SES_ACT_CAPTURE && hover <= SES_ACT_BANK)
							JE_playSampleNum(S_CURSOR);
					}
					else
						act = hover;  // the absolute nav id; the dispatch below matches enum values
				}
			}
			newmouse = false;
		}

		if (newkey)
		{
			notice[0] = '\0';
			int dir = 0;
			if ((lastkey_mod & KMOD_CTRL) != 0)
			{
				const SDL_Keycode key = SDL_GetKeyFromScancode(lastkey_scan);
				if (key == SDLK_z || key == SDLK_y)
				{
					redoMode = (key == SDLK_y);
					act = SES_ACT_HISTORY;
				}
			}
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_TAB:
				canvasFocus = !canvasFocus;
				JE_playSampleNum(S_CURSOR);
				break;
			case SDL_SCANCODE_UP:
			case SDL_SCANCODE_DOWN:
				if (canvasFocus)
					curY = (curY + (lastkey_scan == SDL_SCANCODE_UP ? SE_FRAME_H - 1 : 1)) % SE_FRAME_H;
				else
					selected = (selected + (lastkey_scan == SDL_SCANCODE_UP ? SES_NAV_COUNT - 1 : 1)) % SES_NAV_COUNT;
				if (!canvasFocus && (selected == SES_PAL_COLOR || selected == SES_PAL_BACKGROUND))
					paletteTarget = selected;
				JE_playSampleNum(S_CURSOR);
				break;
			case SDL_SCANCODE_LEFT:
				if (canvasFocus)
					curX = (curX + SE_FRAME_W - 1) % SE_FRAME_W;
				else
					dir = -1;
				break;
			case SDL_SCANCODE_RIGHT:
				if (canvasFocus)
					curX = (curX + 1) % SE_FRAME_W;
				else
					dir = 1;
				break;
			case SDL_SCANCODE_PAGEUP:
			case SDL_SCANCODE_PAGEDOWN:
				frame = (frame + 4 + (lastkey_scan == SDL_SCANCODE_PAGEUP ? -1 : 1)) % 5 + 1;
				JE_playSampleNum(S_CURSOR);
				break;
			case SDL_SCANCODE_LEFTBRACKET:
				color = (color + 256 - seColorStep(tool, false)) % 256;
				break;
			case SDL_SCANCODE_RIGHTBRACKET:
				color = (color + seColorStep(tool, false)) % 256;
				break;
			case SDL_SCANCODE_H:
				flipMode = SES_FLIP_H;
				act = SES_ACT_FLIP;
				break;
			case SDL_SCANCODE_V:
				flipMode = SES_FLIP_V;
				act = SES_ACT_FLIP;
				break;
			case SDL_SCANCODE_1:
			case SDL_SCANCODE_2:
			case SDL_SCANCODE_3:
			case SDL_SCANCODE_4:
			case SDL_SCANCODE_5:
			case SDL_SCANCODE_6:
			case SDL_SCANCODE_7:
			case SDL_SCANCODE_8:
				bank = lastkey_scan - SDL_SCANCODE_1 + 1;
				JE_playSampleNum(S_CURSOR);
				break;
			case SDL_SCANCODE_M:
				mirror = !mirror;
				JE_playSampleNum(S_CURSOR);
				break;
			case SDL_SCANCODE_BACKSPACE:
				if (canvasFocus)
				{
					seUndoBegin(bank);
					*seFramePx(bank, frame, curX, curY) = 0;
					if (mirror)
						*seFramePx(bank, frame, SE_FRAME_W - 1 - curX, curY) = 0;
				}
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_SPACE:
				if (canvasFocus)
				{
					if (tool != SES_TOOL_PICK)
					{
						seUndoBegin(bank);
						seShadeStrokeBegin();
					}
					paintedColor = (tool == SES_TOOL_PAINT || tool == SES_TOOL_FILL);
					seApplyTool(bank, frame, curX, curY, &color, &tool, mirror,
					            (lastkey_mod & KMOD_SHIFT) != 0);
				}
				else if (selected >= SES_NUDGE_LEFT)
					act = selected;
				else if (selected == SES_PAL_COLOR && tool == SES_TOOL_COLORIZE)
					dyeNow = true;
				else
					dir = 1;
				break;
			case SDL_SCANCODE_ESCAPE:
				done = true;
				break;
			default:
				break;
			}

			if (dir != 0)
			{
				switch (selected)
				{
				case SES_ROW_BANK:   bank = (bank + 7 + dir) % 8 + 1; break;
				case SES_ROW_FRAME:  frame = (frame + 4 + dir) % 5 + 1; break;
				case SES_ROW_TOOL:   tool = seCycleTool(tool, dir, &paletteTarget); break;
				case SES_ROW_MIRROR: mirror = !mirror; break;
				case SES_ROW_GUIDES: guides = (guides + SES_GUIDES_COUNT + dir) % SES_GUIDES_COUNT; break;
				case SES_ROW_SOURCE:
					if (sourceCount > 0)
						source = (source + sourceCount + dir) % sourceCount;
					break;
				case SES_PAL_COLOR:
					color = (color + 256 + dir * seColorStep(tool, (lastkey_mod & KMOD_SHIFT) != 0)) % 256;
					break;
				case SES_PAL_BACKGROUND: background = (background + 256 + ((lastkey_mod & KMOD_SHIFT) ? dir * 16 : dir)) % 256; break;
				case SES_ACT_CAPTURE: captureAll = !captureAll; break;
				case SES_ACT_COPY:   copyPose = (copyPose + 4 + dir) % 5 + 1; break;
				case SES_ACT_FLIP:   flipMode = (flipMode + SES_FLIP_COUNT + dir) % SES_FLIP_COUNT; break;
				case SES_ACT_HISTORY: redoMode = !redoMode; break;
				case SES_ACT_CLEAR:  clearAll = !clearAll; break;
				case SES_ACT_BANK:   bankMode = seCycleBankMode(bankMode, dir); break;
				default:             break;
				}
				JE_playSampleNum(S_CURSOR);
			}
			newkey = false;
		}

		if (paintedColor)
			seTouchColor(recent, &recentCount, (JE_byte)color);

		if (dyeNow)
		{
			seUndoBegin(bank);
			seColorizeFrame(bank, frame, color >> 4);
			JE_playSampleNum(S_SELECT);
		}

		if (act >= SES_NUDGE_LEFT && act != SES_ACT_HISTORY && act != SES_ACT_DONE &&
		    !(act == SES_ACT_BANK && bankMode != SES_BANK_REVERT))
			seUndoBegin(bank);

		switch (act)
		{
		case SES_NUDGE_LEFT:
		case SES_NUDGE_UP:
		case SES_NUDGE_DOWN:
		case SES_NUDGE_RIGHT:
		{
			const int n = act - SES_NUDGE_LEFT;
			seNudgeFrame(bank, frame, nudgeDx[n], nudgeDy[n]);
			JE_playSampleNum(S_SELECT);
			break;
		}
		case SES_ACT_CAPTURE:
		{
			const int first = captureAll ? 1 : frame, last = captureAll ? SE_FRAMES : frame;
			char name[32] = "";
			bool taken = false;
			if (source >= captureHullCount)
			{
				const int src = seCustomSourceBank(source - captureHullCount, bank);
				seCaptureCustom(bank, src, first, last);
				snprintf(name, sizeof(name), "Custom %d", src);
				taken = true;
			}
			else if (captureHullCount > 0 && seCaptureHull(bank, captureHulls[source], first, last))
			{
				JE_shipShortName(captureHulls[source], name, sizeof(name));
				taken = true;
			}
			if (taken)
			{
				snprintf(notice, sizeof(notice), "Captured %.28s", name);
				JE_playSampleNum(S_SELECT);
			}
			else
			{
				SDL_strlcpy(notice, "No compatible hull", sizeof(notice));
				JE_playSampleNum(S_SPRING);
			}
			break;
		}
		case SES_ACT_COPY:
			for (int y = 0; y < SE_FRAME_H; ++y)
				for (int x = 0; x < SE_FRAME_W; ++x)
					*seFramePx(bank, frame, x, y) = *seFramePx(bank, copyPose, x, y);
			JE_playSampleNum(S_SELECT);
			break;
		case SES_ACT_FLIP:
			if (flipMode == SES_FLIP_H || flipMode == SES_FLIP_V)
			{
				seFlipFrame(bank, frame, flipMode == SES_FLIP_V);
			}
			else
			{
				const bool vertical = (flipMode == SES_FOLD_U || flipMode == SES_FOLD_D);
				const bool fromFar = (flipMode == SES_FOLD_R || flipMode == SES_FOLD_D);
				seFoldFrame(bank, frame, vertical, fromFar);
			}
			JE_playSampleNum(S_SELECT);
			break;
		case SES_ACT_HISTORY:
			if (seUndoApply(redoMode, &bank))
				JE_playSampleNum(S_SELECT);
			else
			{
				SDL_strlcpy(notice, redoMode ? "Nothing to redo" : "Nothing to undo",
				            sizeof(notice));
				JE_playSampleNum(S_SPRING);
			}
			break;
		case SES_ACT_CLEAR:
		{
			const int first = clearAll ? 1 : frame, last = clearAll ? SE_FRAMES : frame;
			for (int f = first; f <= last; ++f)
				for (int y = 0; y < SE_FRAME_H; ++y)
					for (int x = 0; x < SE_FRAME_W; ++x)
						*seFramePx(bank, f, x, y) = 0;
			JE_playSampleNum(S_SELECT);
			break;
		}
		case SES_ACT_BANK:
			if (bankMode == SES_BANK_EXPORT || bankMode == SES_BANK_IMPORT)
			{
				const bool moved = (bankMode == SES_BANK_EXPORT)
				                 ? seStripExportFile(bank, notice, sizeof(notice))
				                 : seStripImportFile(bank, notice, sizeof(notice));
				JE_playSampleNum(moved ? S_SELECT : S_SPRING);
				break;
			}
			for (int f = 1; f <= SE_FRAMES; ++f)
				for (int corner = 0; corner < 4; ++corner)
				{
					const unsigned i = seCellIndex(bank, f, corner);
					memcpy(seCells[i], seCellsSaved[i], SE_CELL_BYTES);
				}
			SDL_strlcpy(notice, "Bank reverted", sizeof(notice));
			JE_playSampleNum(S_SELECT);
			break;
		case SES_ACT_DONE:
			done = true;
			break;
		default:
			break;
		}
	}

	mouseShiftKeepsCursor = false;
	mouseTwoFingerRightClick = false;

	// Loadout previews read the compiled blob.
	seRebuildShapes();

	wait_noinput_consume(false, false, true);
}

void JE_shipEditor(bool fromOutpost)
{
	seFromOutpost = fromOutpost;
	// The title screen has not necessarily loaded item data or shop sprites.
	if (weaponPort[1].name[0] == '\0')
		JE_loadItemDat();
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');

	if (!extraAvail)
		seSeedDefaults();

	// Normalize hand-edited or incompatible fields before cycling them.
	seNormalizeShips();

	JE_ShipsType savedTable;
	memcpy(savedTable, extraShips, sizeof(savedTable));
	const bool savedExtraAvail = extraAvail;
	const bool savedExtraUserAvail = extraUserAvail;

	for (unsigned i = 1; i <= SE_BLOB_SPRITES; ++i)
		seDecodeCell(&extraShapes, i, seCells[i]);
	memcpy(seCellsSaved, seCells, sizeof(seCells));

	const bool prevCentered = (video_get_menu_x_offset() != 0);
	set_menu_centered(true);

	Palette savedPalette;
	memcpy(savedPalette, colors, sizeof(Palette));  // restored on exit

	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg;

	// Item icons require the shop palette.
	JE_loadPic(VGAScreen, 1, true);
	memcpy(VGAScreen2->pixels, VGAScreen->pixels, (size_t)VGAScreen2->pitch * VGAScreen2->h);

	const int panX0 = 150, panX1 = 313, panY0 = 7, panY1 = 183;
	const int fieldsTop = panY0 + 13;
	const int row_h = 12;
	const int actionsTop = panY1 - 11;
	const int previewTop = actionsTop - row_h - 2;
	const int defaultsTop = previewTop - row_h;
	const int panMidX = (panX0 + panX1) / 2;
	const int labelX = panX0 + 5, valueX = panX1 - 5;
	const int boxMid = (SE_BOX_X0 + SE_BOX_X1 + 1) / 2;
	const int sideLabelsY = SE_BOX_Y0 + 104;
	const int itemLabelsY = SE_BOX_Y0 + 153, hpBarsY = SE_BOX_Y0 + 166;

	int slot = 1;
	int selected = 0;
	bool defaultsArmed = false;
	bool revertArmed = false;
	Uint32 previewStart = SDL_GetTicks();
	Uint32 starClock = 0;
	Uint32 starLast = SDL_GetTicks();
	memset(seMountAnim, 0, sizeof(seMountAnim));
	int previewSlot = slot;
	JE_byte previewGraphic = *seField(slot, SE_ROW_GRAPHIC);
	char notice[40] = "";
	int prev_mx = mouse_x, prev_my = mouse_y;
	bool legacyAvailable = seLegacyUserShapesAvailable();
	bool forceSave = false;
	bool done = false;

	wait_noinput_consume(false, false, true);
	mouseTwoFingerRightClick = true;

	const EditorMusicTick musicTick = seMusicTick();
	while (!done)
	{
		setDelay(EDITOR_TICK_DELAY);
		editorServiceNetwork();
		if (musicTick != NULL)
			musicTick();

		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		Sprite2_array *sheet = &spriteSheet9;
		// the editor always edits the local file
		const JE_word gr = JE_SGr(EXTRA_SHIPS_LOCAL_SEAT, slot, &sheet);
		const JE_byte graphic = *seField(slot, SE_ROW_GRAPHIC);
		if (slot != previewSlot || graphic != previewGraphic)
		{
			previewStart = SDL_GetTicks();
			previewSlot = slot;
			previewGraphic = graphic;
		}

		const Uint32 starNow = SDL_GetTicks();
		starClock += starNow - starLast;
		starLast = starNow;

		seDrawPreviewBackdrop();
		JE_rectangle(VGAScreen, SE_BOX_X0 - 1, SE_BOX_Y0 - 1, SE_BOX_X1 + 1, SE_BOX_Y1 + 1, C_HI);

		char caption[16];
		snprintf(caption, sizeof(caption), "Tab + %d", slot % 10);
		draw_font_hv_shadow(VGAScreen, boxMid, SE_BOX_Y0 + 5, caption, small_font, centered, 5, 4, true, 1);

		if (gr > 1)
		{
			for (int b = -2; b <= 2; ++b)
				blit_sprite2x2(VGAScreen, SE_BOX_X0 + 4 + (b + 2) * 26, SE_POSES_Y, *sheet, gr + b * 2);
		}
		else
			seDrawHull(boxMid, SE_POSES_Y, sheet, gr, 0);

		JE_drawItem(6, *seField(slot, SE_ROW_LEFT), SE_KICK_L_X, SE_CENTER_Y);
		seDrawHull(boxMid, SE_CENTER_Y, sheet, gr,
		           extraShipPreviewBank(SDL_GetTicks() - previewStart));
		JE_drawItem(7, *seField(slot, SE_ROW_RIGHT), SE_KICK_R_X, SE_CENTER_Y);
		{
			const int sw = JE_textWidth("SIDE ", small_font);
			const int lx = SE_BOX_X0 + 17 - JE_textWidth("SIDE L", small_font) / 2;
			const int rx = SE_BOX_X1 - 16 - JE_textWidth("SIDE R", small_font) / 2;
			draw_font_hv_shadow(VGAScreen, lx + 1, sideLabelsY, "SIDE", small_font, left_aligned, 5, 2, true, 1);
			draw_font_hv_shadow(VGAScreen, lx + sw, sideLabelsY, "L", small_font, left_aligned, 5, 2, true, 1);
			draw_font_hv_shadow(VGAScreen, rx + 1, sideLabelsY, "SIDE", small_font, left_aligned, 5, 2, true, 1);
			draw_font_hv_shadow(VGAScreen, rx + sw, sideLabelsY, "R", small_font, left_aligned, 5, 2, true, 1);
		}

		{
			// Type 0 uses the composed special icon.
			static const struct { JE_byte type; int row; const char *tag; } icons[5] = {
				{ 2, SE_ROW_FRONT, "FRNT" }, { 3, SE_ROW_REAR, "REAR" }, { 0, SE_ROW_SPECIAL, "SPEC" },
				{ 5, SE_ROW_GENERATOR, "GEN" }, { 4, SE_ROW_SHIELD, "SHLD" },
			};
			for (int i = 0; i < 5; ++i)
			{
				const int x = SE_BOX_X0 + 2 + i * 27;
				const JE_byte v = *seField(slot, icons[i].row);
				if (icons[i].type == 0)
				{
					if (v != 0 && debug_special_is_safe(v))
						draw_special_icon(VGAScreen, x, SE_ITEMS_Y, v);
				}
				else
				{
					JE_drawItem(icons[i].type,
					            extraShipResolvePort((uint)customWeaponLocalOwner(), v),
					            x, SE_ITEMS_Y);
				}
				draw_font_hv_shadow(VGAScreen, x + 12, itemLabelsY, icons[i].tag, small_font, centered, 5, 2, true, 1);
			}
		}

		{
			int shieldMax = 1;
			for (int s = 1; s <= SHIELD_NUM; ++s)
				if (shields[s].mpwr > shieldMax)
					shieldMax = shields[s].mpwr;
			draw_corner_clamps(VGAScreen, SE_BOX_X0 + 9, hpBarsY - 3, SE_BOX_X0 + 12 + 112 + 2,
			                   hpBarsY + 2 * ENEMY_BAR_THICK + 1, 2, SE_BG_CLAMP);
			hud_draw_ship_hp_bars_preview(SE_BOX_X0 + 12, hpBarsY, 112,
			                              shields[*seField(slot, SE_ROW_SHIELD)].mpwr, (uint)shieldMax,
			                              *seField(slot, SE_ROW_ARMOR), 30);
		}

		fill_rectangle_xy(VGAScreen, panX0, panY0, panX1, panY1, C_PANEL);
		JE_rectangle(VGAScreen, panX0, panY0, panX1, panY1, C_HI);
		draw_font_hv_shadow(VGAScreen, panMidX, panY0 + 2, "SHIP EDITOR", small_font,
		                    centered, 15, 3, false, 1);
		fill_rectangle_xy(VGAScreen, panX0 + 2, panY0 + 10, panX1 - 2, panY0 + 10, C_DIV);

		for (int r = 0; r < SE_ROW_COUNT; ++r)
		{
			const int ry = fieldsTop + r * row_h + (r > SE_ROW_SLOT ? 2 : 0);
			const bool sel = (selected == r);
			fill_rectangle_xy(VGAScreen, panX0 + 2, ry - 1, panX1 - 2, ry + row_h - 3, sel ? C_SEL : C_PANEL);

			char raw[40], val[48];
			const char *text = seValueText(r, slot, raw, sizeof(raw));
			if (sel)
				snprintf(val, sizeof(val), "< %s >", text);
			else
				SDL_strlcpy(val, text, sizeof(val));

			// Long item names may need the whole row; drop the label when they collide.
			if (labelX + JE_textWidth(seRows[r].label, small_font) + 6 <= valueX - JE_textWidth(val, small_font))
				draw_font_hv_shadow(VGAScreen, labelX, ry, seRows[r].label, small_font, left_aligned, 15, sel ? 5 : 3, false, 1);
			draw_font_hv_shadow(VGAScreen, valueX, ry, val, small_font, right_aligned, 15, sel ? 6 : 5, false, 1);
		}

		fill_rectangle_xy(VGAScreen, panX0 + 2, fieldsTop + row_h - 1, panX1 - 2, fieldsTop + row_h - 1, C_DIV);
		fill_rectangle_xy(VGAScreen, panX0 + 2, defaultsTop - 5, panX1 - 2, defaultsTop - 5, C_DIV);

		{
			const bool sel = selected == SE_RESTORE_DEFAULTS;
			fill_rectangle_xy(VGAScreen, panX0 + 2, defaultsTop - 1, panX1 - 2, defaultsTop + row_h - 3,
			                  sel ? C_SEL : C_PANEL);
			draw_font_hv_shadow(VGAScreen, labelX, defaultsTop, "Defaults", small_font,
			                    left_aligned, 15, sel ? 5 : 3, false, 1);
			const char *const value = defaultsArmed ? "Confirm" : "Restore";
			char shown[16];
			if (sel)
				snprintf(shown, sizeof(shown), "< %s >", value);
			else
				SDL_strlcpy(shown, value, sizeof(shown));
			draw_font_hv_shadow(VGAScreen, valueX, defaultsTop, shown, small_font,
			                    right_aligned, 15, sel ? 6 : 5, false, 1);
		}

		{
			const bool sel = selected == SE_TOGGLE_STARS;
			char value[12];
			if (sel)
				snprintf(value, sizeof(value), "< %s >", shipEditorStars ? "On" : "Off");
			else
				SDL_strlcpy(value, shipEditorStars ? "On" : "Off", sizeof(value));
			fill_rectangle_xy(VGAScreen, panX0 + 2, previewTop - 1, panX1 - 2,
			                  previewTop + row_h - 3, sel ? C_SEL : C_PANEL);
			draw_font_hv_shadow(VGAScreen, labelX, previewTop, "Star Background", small_font,
			                    left_aligned, 15, sel ? 5 : 3, false, 1);
			draw_font_hv_shadow(VGAScreen, valueX, previewTop, value, small_font,
			                    right_aligned, 15, sel ? 6 : 5, false, 1);
		}

		fill_rectangle_xy(VGAScreen, panX0 + 2, actionsTop - 3, panX1 - 2, actionsTop - 3, C_DIV);
		const int actionWidth = (panX1 - panX0 - 4) / SE_ACT_COUNT;
		for (int a = 0; a < SE_ACT_COUNT; ++a)
		{
			const int bx0 = panX0 + 2 + a * actionWidth;
			const int bx1 = (a == SE_ACT_COUNT - 1) ? panX1 - 2 : bx0 + actionWidth - 2;
			const bool enabled = a != (SE_ACT_IMPORT - SE_ACT_SPRITES) || legacyAvailable;
			const bool sel = (selected == SE_ACT_SPRITES + a);
			const char *const label = a == SE_ACT_REVERT - SE_ACT_SPRITES && revertArmed
			                        ? "Confirm" : seActs[a].label;
			fill_rectangle_xy(VGAScreen, bx0, actionsTop - 1, bx1, actionsTop + 9, sel ? C_SEL : C_DIV);
			draw_font_hv_shadow(VGAScreen, (bx0 + bx1) / 2, actionsTop, label, small_font, centered, 15,
			                    enabled ? (sel ? 6 : 4) : -5, false, 1);
		}

		const char *const help = selected < SE_ROW_COUNT ? seRows[selected].help
		                         : selected == SE_RESTORE_DEFAULTS
		                             ? (defaultsArmed ? seDefaultsConfirmHelp : seDefaultsHelp)
		                         : selected == SE_TOGGLE_STARS              ? seStarsHelp
		                         : selected == SE_ACT_REVERT && revertArmed ? seRevertConfirmHelp
		                         : seActs[selected - SE_ACT_SPRITES].help;
		editorDrawHint(notice[0] != '\0' ? notice : help);

		touch_ui_set_layout(TOUCH_LAYOUT_LIST);

		push_joysticks_as_keyboard();
		service_SDL_events(false);

		mouseCursor = MOUSE_POINTER_NORMAL;
		sePresentPreviewTick(&starClock, &starLast,
		                     *seField(slot, SE_ROW_LEFT), *seField(slot, SE_ROW_RIGHT));

		// Do not leave with unsaved changes.
		bool leave = false;
		bool revert = false;
		bool loadDefaults = false;
		bool openSprites = false;
		bool importLegacy = false;

		if (mouse_scroll != 0)
		{
			selected -= mouse_scroll;
			selected = selected < 0 ? 0 : (selected >= SE_NAV_COUNT ? SE_NAV_COUNT - 1 : selected);
			mouse_scroll = 0;
		}

		int hover = -1;
		if (mouse_x >= panX0 && mouse_x <= panX1)
		{
			if (mouse_y >= fieldsTop - 1 && mouse_y < fieldsTop + row_h - 1)
				hover = SE_ROW_SLOT;
			else if (mouse_y >= fieldsTop + row_h + 1 && mouse_y < fieldsTop + SE_ROW_COUNT * row_h + 1)
				hover = (mouse_y - (fieldsTop + 1)) / row_h;
			else if (mouse_y >= defaultsTop - 1 && mouse_y < defaultsTop + row_h - 2)
				hover = SE_RESTORE_DEFAULTS;
			else if (mouse_y >= previewTop - 1 && mouse_y < previewTop + row_h - 2)
				hover = SE_TOGGLE_STARS;
			else if (mouse_y >= actionsTop - 1 && mouse_y <= actionsTop + 9)
			{
				int a = (mouse_x - (panX0 + 2)) / actionWidth;
				if (a >= SE_ACT_COUNT)
					a = SE_ACT_COUNT - 1;
				hover = SE_ACT_SPRITES + (a < 0 ? 0 : a);
			}
		}
		if (hover >= 0 && hover < SE_NAV_COUNT && (mouse_x != prev_mx || mouse_y != prev_my) && hover != selected)
		{
			JE_playSampleNum(S_CURSOR);
			selected = hover;
		}
		prev_mx = mouse_x;
		prev_my = mouse_y;

		if (newmouse)
		{
			notice[0] = '\0';
			const int dir = (lastmouse_but == SDL_BUTTON_RIGHT) ? -1 : 1;

			int boxRow = -1;
			bool boxSlot = false;
			int fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0;
			if (mouse_x >= SE_BOX_X0 && mouse_x <= SE_BOX_X1 &&
			    mouse_y >= SE_BOX_Y0 && mouse_y <= SE_BOX_Y1)
			{
				static const int cellRow[5] = {
					SE_ROW_FRONT, SE_ROW_REAR, SE_ROW_SPECIAL, SE_ROW_GENERATOR, SE_ROW_SHIELD
				};
				if (mouse_y < SE_POSES_Y - 3)
				{
					boxSlot = true;
					fx0 = SE_BOX_X0; fy0 = SE_BOX_Y0; fx1 = SE_BOX_X1; fy1 = SE_POSES_Y - 4;
				}
				else if (mouse_y <= SE_POSES_Y + 30)
				{
					boxRow = SE_ROW_GRAPHIC;
					fx0 = SE_BOX_X0; fy0 = SE_POSES_Y - 2; fx1 = SE_BOX_X1; fy1 = SE_POSES_Y + 29;
				}
				else if (mouse_y >= SE_CENTER_Y - 6 && mouse_y < SE_ITEMS_Y - 7)
				{
					boxRow = (mouse_x <= SE_KICK_L_X + 50) ? SE_ROW_LEFT
					       : (mouse_x >= SE_KICK_R_X - 27) ? SE_ROW_RIGHT : SE_ROW_GRAPHIC;
					fx0 = (boxRow == SE_ROW_LEFT) ? SE_KICK_L_X - 1
					    : (boxRow == SE_ROW_RIGHT) ? SE_KICK_R_X - 1 : SE_KICK_L_X + 51;
					fx1 = (boxRow == SE_ROW_LEFT) ? SE_KICK_L_X + 24
					    : (boxRow == SE_ROW_RIGHT) ? SE_KICK_R_X + 24 : SE_KICK_R_X - 28;
					fy0 = SE_CENTER_Y - 1;
					fy1 = SE_CENTER_Y + 28;
				}
				else if (mouse_y >= SE_ITEMS_Y - 2 && mouse_y <= SE_ITEMS_Y + 43)
				{
					int cell = (mouse_x - SE_BOX_X0) / 27;
					if (cell > 4)
						cell = 4;
					boxRow = cellRow[cell];
					fx0 = SE_BOX_X0 + cell * 27 + 1; fy0 = SE_ITEMS_Y - 1;
					fx1 = SE_BOX_X0 + cell * 27 + 26; fy1 = SE_ITEMS_Y + 28;
				}
			}

			if (boxSlot)
			{
				slot += (dir < 0) ? (slot > 1 ? -1 : 9) : (slot < 10 ? 1 : -9);
				selected = SE_ROW_SLOT;
				editorFlashStart(&seFlash, fx0, fy0, fx1, fy1);
				JE_playSampleNum(S_CURSOR);
			}
			else if (boxRow == SE_ROW_GRAPHIC && dir > 0)
			{
				selected = boxRow;
				sePickGraphic(slot);
			}
			else if (boxRow >= 0)
			{
				seStepField(slot, boxRow, dir);
				selected = boxRow;
				editorFlashStart(&seFlash, fx0, fy0, fx1, fy1);
				JE_playSampleNum(S_CURSOR);
			}
			else if (hover >= 0 && hover < SE_NAV_COUNT)
			{
				selected = hover;
				if (hover == SE_ROW_SLOT)
				{
					const int step = (mouse_x < panMidX) ? -dir : dir;
					slot += (step < 0) ? (slot > 1 ? -1 : 9) : (slot < 10 ? 1 : -9);
					JE_playSampleNum(S_CURSOR);
				}
				else if (hover == SE_ROW_GRAPHIC && dir > 0)
				{
					sePickGraphic(slot);
				}
				else if (hover < SE_ROW_COUNT)
				{
					seStepField(slot, hover, (mouse_x < panMidX) ? -dir : dir);
					JE_playSampleNum(S_CURSOR);
				}
				else if (hover == SE_TOGGLE_STARS)
				{
					shipEditorStars = !shipEditorStars;
					JE_playSampleNum(S_CURSOR);
				}
				else if (dir > 0)
				{
					if (hover == SE_RESTORE_DEFAULTS)
						loadDefaults = true;
					else if (hover == SE_ACT_SPRITES)
						openSprites = true;
					else if (hover == SE_ACT_IMPORT)
					{
						if (legacyAvailable)
							importLegacy = true;
						else
						{
							SDL_strlcpy(notice, "User.shp not found", sizeof(notice));
							JE_playSampleNum(S_SPRING);
						}
					}
					else if (hover == SE_ACT_REVERT)
						revert = true;
					else if (hover == SE_ACT_DONE)
						leave = true;
				}
			}
			newmouse = false;
		}

		if (newkey)
		{
			notice[0] = '\0';
			int dir = 0;
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				selected = (selected + SE_NAV_COUNT - 1) % SE_NAV_COUNT;
				JE_playSampleNum(S_CURSOR);
				break;
			case SDL_SCANCODE_DOWN:
				selected = (selected + 1) % SE_NAV_COUNT;
				JE_playSampleNum(S_CURSOR);
				break;
			case SDL_SCANCODE_LEFT:
				dir = -1;
				break;
			case SDL_SCANCODE_RIGHT:
				dir = 1;
				break;
			case SDL_SCANCODE_PAGEUP:
			case SDL_SCANCODE_PAGEDOWN:
				slot += (lastkey_scan == SDL_SCANCODE_PAGEUP) ? (slot > 1 ? -1 : 9) : (slot < 10 ? 1 : -9);
				JE_playSampleNum(S_CURSOR);
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_SPACE:
				if (selected == SE_RESTORE_DEFAULTS)
					loadDefaults = true;
				else if (selected == SE_TOGGLE_STARS)
				{
					shipEditorStars = !shipEditorStars;
					JE_playSampleNum(S_CURSOR);
				}
				else if (selected == SE_ACT_SPRITES)
					openSprites = true;
				else if (selected == SE_ACT_IMPORT)
				{
					if (legacyAvailable)
						importLegacy = true;
					else
					{
						SDL_strlcpy(notice, "User.shp not found", sizeof(notice));
						JE_playSampleNum(S_SPRING);
					}
				}
				else if (selected == SE_ACT_REVERT)
					revert = true;
				else if (selected == SE_ACT_DONE)
					leave = true;
				else if (selected == SE_ROW_GRAPHIC)
					sePickGraphic(slot);
				else
					dir = 1;  // Enter cycles a field forward
				break;
			case SDL_SCANCODE_ESCAPE:
				leave = true;
				break;
			default:
				if (lastkey_scan >= SDL_SCANCODE_1 && lastkey_scan <= SDL_SCANCODE_0)
				{
					slot = lastkey_scan - SDL_SCANCODE_1 + 1;
					JE_playSampleNum(S_CURSOR);
				}
				break;
			}

			if (dir != 0)
			{
				bool playCursor = true;
				if (selected == SE_ROW_SLOT)
					slot += (dir < 0) ? (slot > 1 ? -1 : 9) : (slot < 10 ? 1 : -9);
				else if (selected < SE_ROW_COUNT)
				{
					// Shift steps armor by ten.
					const int reps = (selected == SE_ROW_ARMOR && (lastkey_mod & KMOD_SHIFT)) ? 10 : 1;
					for (int i = 0; i < reps; ++i)
						seStepField(slot, selected, dir);
				}
				else if (selected == SE_RESTORE_DEFAULTS)
				{
					loadDefaults = true;
					playCursor = false;
				}
				else if (selected == SE_TOGGLE_STARS)
					shipEditorStars = !shipEditorStars;
				else
				{
					selected += dir;
					selected = selected < SE_ACT_SPRITES ? SE_ACT_SPRITES
					         : (selected > SE_ACT_DONE ? SE_ACT_DONE : selected);
				}
				if (playCursor)
					JE_playSampleNum(S_CURSOR);
			}
			newkey = false;
		}

		if (selected != SE_RESTORE_DEFAULTS && defaultsArmed)
		{
			defaultsArmed = false;
			notice[0] = '\0';
		}
		if (selected != SE_ACT_REVERT && revertArmed)
		{
			revertArmed = false;
			notice[0] = '\0';
		}

		if (loadDefaults && !seConfirmAction(&defaultsArmed))
		{
			loadDefaults = false;
			SDL_strlcpy(notice, "Choose Defaults again to confirm", sizeof(notice));
			JE_playSampleNum(S_CURSOR);
		}

		if (loadDefaults)
		{
			if (seLoadStockDefaults())
			{
				forceSave = true;
				previewStart = SDL_GetTicks();
				previewSlot = slot;
				previewGraphic = *seField(slot, SE_ROW_GRAPHIC);
				SDL_strlcpy(notice, "Stock defaults loaded", sizeof(notice));
				JE_playSampleNum(S_SELECT);
			}
			else
			{
				SDL_strlcpy(notice, "Stock newsh$.shp not found", sizeof(notice));
				JE_playSampleNum(S_SPRING);
			}
		}

		if (openSprites)
		{
			const JE_byte grByte = *seField(slot, SE_ROW_GRAPHIC);
			seSpriteEditor(extraShipGraphicIsCustom(grByte) ? grByte - 7 : 1);
			mouseTwoFingerRightClick = true;
		}

		if (importLegacy)
		{
			const char *location = NULL;
			FILE *const f = extraShipsOpenLegacyUserShapes(&location);
			const bool found = f != NULL;
			const bool imported = found && seImportLegacyUserShapes(f);
			if (f != NULL)
				fclose(f);
			if (imported)
			{
				seNormalizeShips();
				forceSave = true;
				SDL_strlcpy(notice, "Imported User.shp; choose Done to save", sizeof(notice));
				JE_playSampleNum(S_SELECT);
			}
			else
			{
				legacyAvailable = found;
				SDL_strlcpy(notice, found ? "Invalid User.shp" : "User.shp not found", sizeof(notice));
				JE_playSampleNum(S_SPRING);
			}
		}

		if (revert && !seConfirmAction(&revertArmed))
		{
			revert = false;
			SDL_strlcpy(notice, "Choose Revert again to confirm", sizeof(notice));
			JE_playSampleNum(S_CURSOR);
		}

		if (revert)
		{
			memcpy(extraShips, savedTable, sizeof(savedTable));
			memcpy(seCells, seCellsSaved, sizeof(seCells));
			seRebuildShapes();
			extraAvail = savedExtraAvail;
			extraUserAvail = savedExtraUserAvail;
			forceSave = false;
			SDL_strlcpy(notice, "Reverted", sizeof(notice));
			JE_playSampleNum(S_SELECT);
		}

		if (leave)
		{
			const bool tableChanged = memcmp(savedTable, extraShips, sizeof(savedTable)) != 0;
			const bool cellsChanged = memcmp(seCellsSaved, seCells, sizeof(seCells)) != 0;
			if (!tableChanged && !cellsChanged && !forceSave)
				done = true;
			else
			{
				// Custom graphics require a sprite blob, even when blank.
				if (extraShapes.size == 0)
					for (int s = 1; s <= 10; ++s)
						if (extraShipGraphicIsCustom(*seField(s, SE_ROW_GRAPHIC)))
						{
							seRebuildShapes();
							break;
						}

				if (JE_saveExtraShapes())
				{
					done = true;
					JE_playSampleNum(S_SELECT);
				}
				else
				{
					SDL_strlcpy(notice, "Could not save custom ships", sizeof(notice));
					JE_playSampleNum(S_SPRING);
				}
			}
		}
	}

	mouseTwoFingerRightClick = false;
	touch_ui_clear_layout();
	wait_noinput(false, false, true);

	editorEndMusicFade();
	spritePickerRelease();

	VGAScreen = temp_surface;
	set_menu_centered(prevCentered);
	memcpy(colors, savedPalette, sizeof(Palette));  // restore the caller's palette
	set_palette(colors, 0, 255);
}
