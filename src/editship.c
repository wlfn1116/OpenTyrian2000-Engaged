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
#include "editship.h"

#include "config.h"
#include "custom_weapon.h"
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "fonthand.h"
#include "game_menu.h"
#include "joystick.h"
#include "keyboard.h"
#include "lvlmast.h"
#include "mainint.h"
#include "mouse.h"
#include "network.h"
#include "nortsong.h"
#include "opentyr.h"
#include "palette.h"
#include "picload.h"
#include "sndmast.h"
#include "sprite.h"
#include "touch_ui.h"
#include "varz.h"
#include "vga256d.h"
#include "video.h"

#include "SDL.h"

#include <stdio.h>
#include <string.h>

#define SAS (sizeof(JE_ShipsType) - 4)

static const JE_byte extraCryptKey[10] = { 58, 23, 16, 192, 254, 82, 113, 147, 62, 99 };

JE_boolean extraAvail;
JE_ShipsType extraShips;
Sprite2_array extraShapes;
static bool extraUserAvail;

// Decrypt in place only when all four plaintext checksums match.
JE_boolean JE_decryptShips(void)
{
	JE_boolean correct = true;
	JE_ShipsType s2;
	JE_byte y;

	for (int x = SAS - 1; x >= 0; x--)
	{
		// (unsigned) only to make the index's non-negativity local; x is >= 0 by the loop condition.
		const unsigned int k = (unsigned)(x + 1) % 10;
		OT_ASSUME(k < 10);
		s2[x] = extraShips[x] ^ extraCryptKey[k];
		if (x > 0)
			s2[x] ^= extraShips[x - 1];
	}  /*  <= Key Decryption Test (Reversed key) */

	y = 0;
	for (uint x = 0; x < SAS; x++)
		y += s2[x];
	if (extraShips[SAS + 0] != y)
		correct = false;

	y = 0;
	for (uint x = 0; x < SAS; x++)
		y -= s2[x];
	if (extraShips[SAS + 1] != y)
		correct = false;

	y = 1;
	for (uint x = 0; x < SAS; x++)
		y = y * s2[x] + 1;
	if (extraShips[SAS + 2] != y)
		correct = false;

	y = 0;
	for (uint x = 0; x < SAS; x++)
		y ^= s2[x];
	if (extraShips[SAS + 3] != y)
		correct = false;

	if (correct)
		memcpy(extraShips, s2, sizeof(extraShips));

	return correct;
}

// Encrypt the table and append its four checksums.
void JE_encryptShips(JE_ShipsType dst)
{
	JE_byte y;

	for (uint x = 0; x < SAS; x++)
	{
		dst[x] = extraShips[x] ^ extraCryptKey[(x + 1) % 10];
		if (x > 0)
			dst[x] ^= dst[x - 1];
	}

	y = 0;
	for (uint x = 0; x < SAS; x++)
		y += extraShips[x];
	dst[SAS + 0] = y;

	y = 0;
	for (uint x = 0; x < SAS; x++)
		y -= extraShips[x];
	dst[SAS + 1] = y;

	y = 1;
	for (uint x = 0; x < SAS; x++)
		y = y * extraShips[x] + 1;
	dst[SAS + 2] = y;

	y = 0;
	for (uint x = 0; x < SAS; x++)
		y ^= extraShips[x];
	dst[SAS + 3] = y;
}

void JE_loadExtraShapes(void)
{
	JE_freeExtraShapes();
	extraUserAvail = false;

	// Prefer the edited file, then the stock Tyrian 2000 copy.
	FILE *f = dir_fopen(get_user_directory(), "newsh$.shp", "rb");
	const bool fromUser = f != NULL;
	if (f == NULL)
		f = dir_fopen(data_dir(), "newsh$.shp", "rb");
	if (f == NULL)
		return;

	const long file_size = ftell_eof(f);
	if (file_size < (long)sizeof(extraShips) || file_size - (long)sizeof(extraShips) > UINT16_MAX)
	{
		fprintf(stderr, "warning: invalid extra ship file size: %ld\n", file_size);
		fclose(f);
		return;
	}

	extraShapes.size = (size_t)(file_size - (long)sizeof(extraShips));
	if (extraShapes.size > 0)
	{
		extraShapes.data = malloc_die(extraShapes.size);
		fread_die(extraShapes.data, extraShapes.size, 1, f);
	}
	fread_die(extraShips, sizeof(extraShips), 1, f);
	fclose(f);

	if (!JE_decryptShips())
	{
		fprintf(stderr, "warning: newsh$.shp failed its checksums; ignoring it\n");
		JE_freeExtraShapes();
		return;
	}
	extraAvail = true;
	extraUserAvail = fromUser;
}

void JE_freeExtraShapes(void)
{
	free_sprite2s(&extraShapes);
	extraAvail = false;
}

// Both peers keep one identical extra-ship file per player seat.

static JE_ShipsType extraShipsNet[2];
static Sprite2_array extraShapesNet[2];
static bool extraNetAvail[2];

JE_byte *extraShipsFor(uint playerIdx)
{
	return (isNetworkGame && playerIdx < COUNTOF(extraShipsNet)) ? extraShipsNet[playerIdx] : extraShips;
}

Sprite2_array *extraShapesFor(uint playerIdx)
{
	return (isNetworkGame && playerIdx < COUNTOF(extraShapesNet)) ? &extraShapesNet[playerIdx] : &extraShapes;
}

bool extraAvailFor(uint playerIdx)
{
	return (isNetworkGame && playerIdx < COUNTOF(extraNetAvail)) ? extraNetAvail[playerIdx] : extraAvail;
}

enum { EXTRA_SHIPS_WIRE_VERSION = 1 };

static bool JE_saveExtraShapes(void);

// Keep the wire big-endian without adding an SDL_net dependency to console builds.
static Uint32 seRead32(const Uint8 *p)
{
	return ((Uint32)p[0] << 24) | ((Uint32)p[1] << 16) | ((Uint32)p[2] << 8) | p[3];
}

static void seWrite32(Uint32 v, Uint8 *p)
{
	p[0] = (Uint8)(v >> 24);
	p[1] = (Uint8)(v >> 16);
	p[2] = (Uint8)(v >> 8);
	p[3] = (Uint8)v;
}

static size_t extraShipsSerializeMode(Uint8 *buf, size_t max, bool userOnly)
{
	const bool available = extraAvail && (!userOnly || extraUserAvail);
	const size_t total = 6 + sizeof(JE_ShipsType) + (available ? extraShapes.size : 0);
	if (max < total)
		return 0;

	buf[0] = EXTRA_SHIPS_WIRE_VERSION;
	buf[1] = available ? 1 : 0;
	memcpy(&buf[2], extraShips, sizeof(JE_ShipsType));
	seWrite32((Uint32)(available ? extraShapes.size : 0), &buf[2 + sizeof(JE_ShipsType)]);
	if (available && extraShapes.size > 0)
		memcpy(&buf[6 + sizeof(JE_ShipsType)], extraShapes.data, extraShapes.size);
	return total;
}

size_t extraShipsSerialize(Uint8 *buf, size_t max)
{
	return extraShipsSerializeMode(buf, max, false);
}

size_t extraShipsSerializeUser(Uint8 *buf, size_t max)
{
	return extraShipsSerializeMode(buf, max, true);
}

bool extraShipsPayloadValid(const Uint8 *buf, size_t len)
{
	if (buf == NULL || len < 6 + sizeof(JE_ShipsType) ||
	    buf[0] != EXTRA_SHIPS_WIRE_VERSION || buf[1] > 1)
		return false;

	const size_t blobSize = seRead32(&buf[2 + sizeof(JE_ShipsType)]);
	return blobSize <= UINT16_MAX && len == 6 + sizeof(JE_ShipsType) + blobSize &&
	       (buf[1] != 0 || blobSize == 0);
}

bool extraShipsAdopt(uint seat, const Uint8 *buf, size_t len)
{
	if (seat >= COUNTOF(extraShipsNet) || !extraShipsPayloadValid(buf, len))
		return false;
	const size_t blobSize = seRead32(&buf[2 + sizeof(JE_ShipsType)]);

	memcpy(extraShipsNet[seat], &buf[2], sizeof(JE_ShipsType));
	free_sprite2s(&extraShapesNet[seat]);
	if (blobSize > 0)
	{
		extraShapesNet[seat].size = blobSize;
		extraShapesNet[seat].data = malloc_die(blobSize);
		memcpy(extraShapesNet[seat].data, &buf[6 + sizeof(JE_ShipsType)], blobSize);
	}
	extraNetAvail[seat] = buf[1] != 0;
	return true;
}

/* Install a transferred compiled file as this machine's persistent custom ships. Absence clears
 * a receiver-side compile and reloads the shared stock fallback, mirroring the sender exactly. */
bool extraShipsAdoptLocal(const Uint8 *buf, size_t len)
{
	if (!extraShipsPayloadValid(buf, len))
		return false;
	if (buf[1] == 0)
	{
		if (!dir_remove_file(get_user_directory(), "newsh$.shp"))
			return false;
		JE_loadExtraShapes();
		extraShipsNetReset();
		return true;
	}

	const size_t blobSize = seRead32(&buf[2 + sizeof(JE_ShipsType)]);
	Uint8 *blob = NULL;
	if (blobSize > 0)
	{
		blob = malloc(blobSize);
		if (blob == NULL)
			return false;
		memcpy(blob, &buf[6 + sizeof(JE_ShipsType)], blobSize);
	}

	memcpy(extraShips, &buf[2], sizeof(JE_ShipsType));
	JE_freeExtraShapes();
	extraShapes.data = blob;
	extraShapes.size = blobSize;
	extraAvail = true;
	extraShipsNetReset();
	const bool saved = JE_saveExtraShapes();
	extraUserAvail = saved;
	return saved;
}

void extraShipsNetInstallLocal(uint seat)
{
	if (seat >= COUNTOF(extraShipsNet))
		return;
	memcpy(extraShipsNet[seat], extraShips, sizeof(JE_ShipsType));
	free_sprite2s(&extraShapesNet[seat]);
	if (extraAvail && extraShapes.size > 0)
	{
		extraShapesNet[seat].size = extraShapes.size;
		extraShapesNet[seat].data = malloc_die(extraShapes.size);
		memcpy(extraShapesNet[seat].data, extraShapes.data, extraShapes.size);
	}
	extraNetAvail[seat] = extraAvail;
}

void extraShipsNetReset(void)
{
	for (uint seat = 0; seat < COUNTOF(extraShipsNet); ++seat)
	{
		memset(extraShipsNet[seat], 0, sizeof(JE_ShipsType));
		free_sprite2s(&extraShapesNet[seat]);
		extraNetAvail[seat] = false;
	}
}

// Write the SHIPEDIT layout: sprite blob, then encrypted ship table.
static bool JE_saveExtraShapes(void)
{
	JE_ShipsType enc;
	JE_encryptShips(enc);

	FILE *f = dir_fopen(get_user_directory(), "newsh$.shp", "wb");
	if (f == NULL)
		return false;

	bool ok = extraShapes.size == 0 || fwrite(extraShapes.data, 1, extraShapes.size, f) == extraShapes.size;
	ok = fwrite(enc, 1, sizeof(enc), f) == sizeof(enc) && ok;
	ok = fclose(f) == 0 && ok;
	if (ok)
	{
		extraAvail = true;  // a fresh compile arms the in-flight Tab/Caps Lock switch
		extraUserAvail = true;
	}
	return ok;
}

/* Ship editor */

enum
{
	SE_ROW_SLOT, SE_ROW_GRAPHIC, SE_ROW_FRONT, SE_ROW_REAR, SE_ROW_SPECIAL,
	SE_ROW_LEFT, SE_ROW_RIGHT, SE_ROW_GENERATOR, SE_ROW_ARMOR, SE_ROW_SHIELD,
	SE_ROW_COUNT,
	SE_ACT_SPRITES = SE_ROW_COUNT,
	SE_ACT_REVERT,
	SE_ACT_DONE,
	SE_NAV_COUNT,
	SE_ACT_COUNT = SE_NAV_COUNT - SE_ROW_COUNT,
};

static const struct { const char *label, *help; } seRows[SE_ROW_COUNT] = {
	{ "Ship",           "In flight: hold Tab + the number (Caps Lock for player 2)." },
	{ "Graphic",        "Seven built-in hulls, plus eight banks you can draw." },
	{ "Front Weapon",   "The main gun this ship flies with." },
	{ "Rear Weapon",    "The rear gun, or None." },
	{ "Special",        "The special weapon, or None." },
	{ "Left Sidekick",  "The companion in the left bay, or None." },
	{ "Right Sidekick", "The companion in the right bay, or None." },
	{ "Generator",      "Recharges the shield and feeds the guns." },
	{ "Armor",          "Hull strength, 1 to 99." },
	{ "Shield",         "The shield model fitted to this ship." },
};

static const struct { const char *label, *help; } seActs[SE_ACT_COUNT] = {
	{ "Sprites", "Draw the custom sprite banks, graphics 8 to 15." },
	{ "Revert",  "Throw away every change made since the editor opened." },
	{ "Done",    "Compile the ships to newsh$.shp and leave." },
};

// Bytes 0..8 map directly to the Graphic through Shield rows.
static JE_byte *seField(int slot, int row)
{
	return &extraShips[(slot - 1) * 15 + (row - SE_ROW_GRAPHIC)];
}

// Resolve the sentinel independently of the machine-local Weapon Creator toggle.
JE_byte extraShipResolvePort(uint seat, JE_byte port)
{
	if (port != EXTRA_SHIP_CUSTOM_PORT)
		return port;

	const int reserved = (seat < CUSTOM_WEAPON_OWNERS) ? customWeaponOwnerPort[seat] : 0;
	return (reserved > 0) ? (JE_byte)reserved : 0;
}

static bool seCustomPortAvailable(void)
{
	return customWeaponEnabled && customWeaponPort > 0;
}

bool extraShipsUseCustomWeapon(void)
{
	if (!extraAvail)
		return false;

	for (int slot = 0; slot < 10; ++slot)
	{
		const JE_byte *const record = &extraShips[slot * 15];
		if (record[1] == EXTRA_SHIP_CUSTOM_PORT || record[2] == EXTRA_SHIP_CUSTOM_PORT)
			return true;
	}
	return false;
}

static bool seValueOk(int row, int v)
{
	switch (row)
	{
	case SE_ROW_GRAPHIC:
		return v >= 1 && v <= 15;
	case SE_ROW_FRONT:
		return v == EXTRA_SHIP_CUSTOM_PORT ||
		       (v >= 1 && v <= PORT_NUM && shop_weapon_port_bay(v) == SHOP_BAY_FRONT);
	case SE_ROW_REAR:
		return v == 0 || v == EXTRA_SHIP_CUSTOM_PORT ||
		       (v <= PORT_NUM && shop_weapon_port_bay(v) == SHOP_BAY_REAR);
	case SE_ROW_SPECIAL:
		// The HUD needs a valid icon for every equipped special.
		return v == 0 || debug_special_is_safe(v);
	case SE_ROW_LEFT:
	case SE_ROW_RIGHT:
		return v == 0 || (v <= OPTION_NUM && options[v].name[0] != '\0');
	case SE_ROW_GENERATOR:
		return v >= 1 && v <= POWER_NUM;
	case SE_ROW_ARMOR:
		return v >= 1 && v <= 99;
	case SE_ROW_SHIELD:
		return v >= 1 && v <= SHIELD_NUM;
	}
	return false;
}

// Preserve a stored custom weapon when the editor cannot offer it for selection.
static bool seValueOffered(int row, int v)
{
	if (v == EXTRA_SHIP_CUSTOM_PORT && !seCustomPortAvailable())
		return false;
	return seValueOk(row, v);
}

static void seStepField(int slot, int row, int dir)
{
	JE_byte *const p = seField(slot, row);
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

// Item tables pad names with spaces; layout needs the visible width.
static const char *seTrimName(const char *name, char *buf, size_t bufSize)
{
	size_t n = strlen(name);
	while (n > 0 && name[n - 1] == ' ')
		--n;
	if (n >= bufSize)
		n = bufSize - 1;
	memcpy(buf, name, n);
	buf[n] = '\0';
	return buf;
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
		if (v <= 7)
			snprintf(buf, bufSize, "Built-in %d", v);
		else
			snprintf(buf, bufSize, "Custom bank %d", v - 7);
		return buf;
	case SE_ROW_FRONT:
	case SE_ROW_REAR:
		if (v == EXTRA_SHIP_CUSTOM_PORT)
		{
			snprintf(buf, bufSize, "%s", customWeaponName[0] != '\0' ? customWeaponName : "Custom Weapon");
			return buf;
		}
		return (v == 0 || v > PORT_NUM) ? "None" : seTrimName(weaponPort[v].name, buf, bufSize);
	case SE_ROW_SPECIAL:
		return (v == 0 || v > SPECIAL_NUM) ? "None" : seTrimName(special[v].name, buf, bufSize);
	case SE_ROW_LEFT:
	case SE_ROW_RIGHT:
		return (v == 0 || v > OPTION_NUM) ? "None" : seTrimName(options[v].name, buf, bufSize);
	case SE_ROW_GENERATOR:
		return (v > POWER_NUM) ? "None" : seTrimName(powerSys[v].name, buf, bufSize);
	case SE_ROW_ARMOR:
		snprintf(buf, bufSize, "%d", v);
		return buf;
	case SE_ROW_SHIELD:
		return (v > SHIELD_NUM) ? "None" : seTrimName(shields[v].name, buf, bufSize);
	}
	return "";
}

// Graphic 6 uses the two-piece Dragonwing sentinel.
static void seDrawHull(int cx, int y, Sprite2_array *sheet, JE_word gr)
{
	if (gr <= 1)
	{
		blit_sprite2x2(VGAScreen, cx - 2 * SHOP_WIDE_HULL_HALF, y, *sheet, gr == 0 ? 13 : 220);
		blit_sprite2x2(VGAScreen, cx, y, *sheet, gr == 0 ? 51 : 222);
	}
	else
		blit_sprite2x2(VGAScreen, cx - SHOP_WIDE_HULL_HALF, y, *sheet, gr);
}

/* Wake on input or once per tick. Tick wakes renew the touch layout; pointer wakes keep paint
 * strokes responsive. */
static void seWaitTick(void)
{
	const Uint16 x0 = mouse_x, y0 = mouse_y;

	for (;;)
	{
		if (getDelayTicks() == 0)
			break;
		JE_mouseStart();   // services SDL events + draws the cursor at its live pos
		JE_showVGA();
		JE_mouseReplace(); // restore the pixels under the cursor for the next pass
		if (newkey || newmouse || mouse_scroll != 0 || mouse_x != x0 || mouse_y != y0)
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
static unsigned seCellIndex(int bank, int frame, int corner)
{
	return seBankBase[bank - 1] + (frame - 3) * 2 + (corner & 1) + (corner >= 2 ? 19 : 0);
}

// Decode the Sprite2 stream into a flat cell; color 0 stays transparent.
static void seDecodeCell(const Sprite2_array *sheet, unsigned index, JE_byte *out)
{
	memset(out, 0, SE_CELL_BYTES);
	if (sheet->data == NULL || index < 1 || (size_t)index * sizeof(Uint16) > sheet->size)
		return;
	const Uint16 off = SDL_SwapLE16(((const Uint16 *)sheet->data)[index - 1]);
	if (off >= sheet->size)
		return;

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

// Copy a built-in hull's five poses into a custom bank.
static void seCaptureBank(int bank, int source)
{
	static const JE_word hullGr[6] = { 233, 157, 195, 271, 81, 119 };  // graphics 1..5 and 7

	for (int frame = 1; frame <= SE_FRAMES; ++frame)
		for (int corner = 0; corner < 4; ++corner)
		{
			const unsigned src = hullGr[source - 1] + (frame - 3) * 2 + (corner & 1) + (corner >= 2 ? 19 : 0);
			seDecodeCell(&spriteSheet9, src, seCells[seCellIndex(bank, frame, corner)]);
		}
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

enum
{
	SES_ROW_BANK, SES_ROW_FRAME, SES_ROW_TOOL, SES_ROW_COLOR, SES_ROW_SOURCE, SES_ROW_COUNT,
	SES_ACT_CAPTURE = SES_ROW_COUNT,
	SES_ACT_COPY, SES_ACT_FLIP_H, SES_ACT_FLIP_V, SES_ACT_CLEAR, SES_ACT_REVERT, SES_ACT_DONE,
	SES_NAV_COUNT,
	SES_ACT_COUNT = SES_NAV_COUNT - SES_ROW_COUNT,
};

enum { SES_TOOL_PAINT, SES_TOOL_FILL, SES_TOOL_PICK, SES_TOOL_ERASE, SES_TOOL_COUNT };

static const char *const sesToolName[SES_TOOL_COUNT] = { "Paint", "Fill", "Pick", "Erase" };

static const struct { const char *label, *help; } sesRows[SES_ROW_COUNT] = {
	{ "Bank",      "One of the eight custom banks, graphics 8 to 15." },
	{ "Pose",      "The five turning poses, hard left to hard right." },
	{ "Tool",      "What a canvas press does; Pick reads a color back." },
	{ "Color",     "Paint color. Click the palette below; 0 is transparent." },
	{ "From Hull", "The built-in hull that Capture copies from." },
};

static const struct { const char *label, *help; } sesActs[SES_ACT_COUNT] = {
	{ "Capture",     "Copy that hull's five poses over this whole bank." },
	{ "Copy Center", "Copy the center pose onto this pose." },
	{ "Flip H",      "Mirror this pose left to right." },
	{ "Flip V",      "Mirror this pose top to bottom." },
	{ "Clear",       "Erase this pose." },
	{ "Revert",      "Restore this bank from the last save." },
	{ "Done",        "Back to the loadout editor." },
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

// Join pointer samples so fast strokes do not leave gaps.
static void seStrokeTo(int bank, int frame, int x0, int y0, int x1, int y1, JE_byte c)
{
	const int dx = (x1 > x0) ? x1 - x0 : x0 - x1;
	const int dy = (y1 > y0) ? y0 - y1 : y1 - y0;  // negative magnitude, per Bresenham
	const int sx = (x0 < x1) ? 1 : -1;
	const int sy = (y0 < y1) ? 1 : -1;
	int err = dx + dy;

	for (;;)
	{
		*seFramePx(bank, frame, x0, y0) = c;
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

// Pick selects the color and returns to Paint.
static void seApplyTool(int bank, int frame, int x, int y, int *color, int *tool)
{
	switch (*tool)
	{
	case SES_TOOL_FILL:
		seFloodFill(bank, frame, x, y, (JE_byte)*color);
		break;
	case SES_TOOL_PICK:
		*color = *seFramePx(bank, frame, x, y);
		*tool = SES_TOOL_PAINT;
		break;
	case SES_TOOL_ERASE:
		*seFramePx(bank, frame, x, y) = 0;
		break;
	default:
		*seFramePx(bank, frame, x, y) = (JE_byte)*color;
		break;
	}
}

static void seSpriteEditor(int bank)
{
	enum { BOX_X0 = 8, BOX_Y0 = 8, BOX_X1 = 143, BOX_Y1 = 182 };
	enum { CANV_X = 28, CANV_Y = 22, CANV_SCALE = 4 };
	enum { STRIP_X = 10, STRIP_Y = 147 };
	enum { PAL_X = 199, PAL_Y = 84, PAL_CELL = 4 };
	const int panX0 = 150, panX1 = 313, panY0 = 7, panY1 = 183;
	const int fieldsTop = panY0 + 16;
	const int row_h = 12;
	const int actionsTop = 152;
	const int act_h = 8;
	const int panMidX = (panX0 + panX1) / 2;
	const int labelX = panX0 + 5, valueX = panX1 - 5;
	enum { C_PANEL = 0xF1, C_DIV = 0xF6, C_HI = 0xFB, C_SEL = 0xF5 };

	int frame = 3, color = 15, source = 1, tool = SES_TOOL_PAINT;
	int selected = SES_ROW_BANK;
	bool canvasFocus = false;
	int curX = SE_FRAME_W / 2, curY = SE_FRAME_H / 2;
	JE_byte heldColor = 0;  // the color the pressed mouse button paints while dragging
	int strokeX = -1, strokeY = -1;  // last cell this drag painted; -1 = no stroke in progress
	char notice[40] = "";
	int prev_mx = mouse_x, prev_my = mouse_y;
	bool done = false;

	wait_noinput(false, false, true);
	newkey = newmouse = false;

	while (!done)
	{
		setDelay(3);

		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		fill_rectangle_xy(VGAScreen, BOX_X0, BOX_Y0, BOX_X1, BOX_Y1, 0);
		JE_rectangle(VGAScreen, BOX_X0 - 1, BOX_Y0 - 1, BOX_X1 + 1, BOX_Y1 + 1, C_HI);

		char caption[24];
		snprintf(caption, sizeof(caption), "Bank %d = Graphic %d", bank, bank + 7);
		draw_font_hv_shadow(VGAScreen, (BOX_X0 + BOX_X1) / 2, BOX_Y0 + 4, caption, small_font, centered, 15, 4, false, 1);

		for (int y = 0; y < SE_FRAME_H; ++y)
			for (int x = 0; x < SE_FRAME_W; ++x)
			{
				const JE_byte c = *seFramePx(bank, frame, x, y);
				if (c != 0)
					fill_rectangle_wh(VGAScreen, CANV_X + x * CANV_SCALE, CANV_Y + y * CANV_SCALE,
					                  CANV_SCALE, CANV_SCALE, c);
			}
		JE_rectangle(VGAScreen, CANV_X - 1, CANV_Y - 1,
		             CANV_X + SE_FRAME_W * CANV_SCALE, CANV_Y + SE_FRAME_H * CANV_SCALE, C_DIV);
		if (canvasFocus)
			JE_rectangle(VGAScreen, CANV_X + curX * CANV_SCALE - 1, CANV_Y + curY * CANV_SCALE - 1,
			             CANV_X + curX * CANV_SCALE + CANV_SCALE, CANV_Y + curY * CANV_SCALE + CANV_SCALE, C_HI);

		for (int f = 0; f < SE_FRAMES; ++f)
		{
			seDrawFramePx(STRIP_X + f * 26, STRIP_Y, bank, f + 1);
			if (f + 1 == frame)
				JE_rectangle(VGAScreen, STRIP_X + f * 26 - 1, STRIP_Y - 1,
				             STRIP_X + f * 26 + SE_FRAME_W, STRIP_Y + SE_FRAME_H, C_HI);
		}

		fill_rectangle_xy(VGAScreen, panX0, panY0, panX1, panY1, C_PANEL);
		JE_rectangle(VGAScreen, panX0, panY0, panX1, panY1, C_HI);
		draw_font_hv_shadow(VGAScreen, panX0 + 5, panY0 + 2, "SPRITE EDITOR", small_font, left_aligned, 15, 3, false, 1);
		snprintf(caption, sizeof(caption), "BANK %d", bank);
		draw_font_hv_shadow(VGAScreen, panX1 - 5, panY0 + 2, caption, small_font, right_aligned, 15, 3, false, 1);
		fill_rectangle_xy(VGAScreen, panX0 + 2, panY0 + 11, panX1 - 2, panY0 + 11, C_DIV);

		for (int r = 0; r < SES_ROW_COUNT; ++r)
		{
			const int ry = fieldsTop + r * row_h;
			const bool sel = (selected == r && !canvasFocus);
			fill_rectangle_xy(VGAScreen, panX0 + 2, ry - 1, panX1 - 2, ry + row_h - 3, sel ? C_SEL : C_PANEL);

			char raw[24], val[32];
			switch (r)
			{
			case SES_ROW_BANK:   snprintf(raw, sizeof(raw), "%d of 8", bank); break;
			case SES_ROW_FRAME:  snprintf(raw, sizeof(raw), "%s", sesPoseName[frame - 1]); break;
			case SES_ROW_TOOL:   snprintf(raw, sizeof(raw), "%s", sesToolName[tool]); break;
			case SES_ROW_COLOR:  snprintf(raw, sizeof(raw), "%d", color); break;
			default:             snprintf(raw, sizeof(raw), "%d of 6", source); break;
			}
			if (sel)
				snprintf(val, sizeof(val), "< %s >", raw);
			else
				SDL_strlcpy(val, raw, sizeof(val));
			draw_font_hv_shadow(VGAScreen, labelX, ry, sesRows[r].label, small_font, left_aligned, 15, sel ? 5 : 3, false, 1);
			draw_font_hv_shadow(VGAScreen, valueX, ry, val, small_font, right_aligned, 15, sel ? 6 : 5, false, 1);
			if (r == SES_ROW_COLOR)
			{
				const int sw = valueX - 12 - JE_textWidth(val, small_font);
				fill_rectangle_xy(VGAScreen, sw - 8, ry, sw, ry + 6, (Uint8)color);
				JE_rectangle(VGAScreen, sw - 9, ry - 1, sw + 1, ry + 7, C_DIV);
			}
		}

		for (int c = 0; c < 256; ++c)
			fill_rectangle_wh(VGAScreen, PAL_X + (c % 16) * PAL_CELL, PAL_Y + (c / 16) * PAL_CELL,
			                  PAL_CELL, PAL_CELL, (Uint8)c);
		JE_rectangle(VGAScreen, PAL_X - 1, PAL_Y - 1, PAL_X + 16 * PAL_CELL, PAL_Y + 16 * PAL_CELL, C_DIV);
		JE_rectangle(VGAScreen, PAL_X + (color % 16) * PAL_CELL - 1, PAL_Y + (color / 16) * PAL_CELL - 1,
		             PAL_X + (color % 16) * PAL_CELL + PAL_CELL, PAL_Y + (color / 16) * PAL_CELL + PAL_CELL, C_HI);

		fill_rectangle_xy(VGAScreen, panX0 + 2, actionsTop - 3, panX1 - 2, actionsTop - 3, C_DIV);
		for (int a = 0; a < SES_ACT_COUNT; ++a)
		{
			const bool alone = (a == SES_ACT_COUNT - 1);
			const int bx0 = (alone || a % 2 == 0) ? panX0 + 2 : panMidX + 1;
			const int bx1 = (alone || a % 2 == 1) ? panX1 - 2 : panMidX - 1;
			const int ry = actionsTop + (a / 2) * act_h;
			const bool sel = (selected == SES_ROW_COUNT + a && !canvasFocus);
			fill_rectangle_xy(VGAScreen, bx0, ry - 1, bx1, ry + act_h - 3, sel ? C_SEL : C_DIV);
			draw_font_hv_shadow(VGAScreen, (bx0 + bx1) / 2, ry, sesActs[a].label, small_font, centered,
			                    15, sel ? 6 : 4, false, 1);
		}

		draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, vga_height - 12,
		                    notice[0] != '\0' ? notice
		                    : canvasFocus ? "Arrows move, Enter uses the tool, Backspace erases, Tab leaves."
		                    : selected < SES_ROW_COUNT ? sesRows[selected].help
		                    : sesActs[selected - SES_ROW_COUNT].help,
		                    small_font, centered, 15, 2, false, 1);

		touch_ui_set_layout(TOUCH_LAYOUT_LIST);

		push_joysticks_as_keyboard();
		service_SDL_events(false);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		seWaitTick();

		const bool mouseMoved = (mouse_x != prev_mx || mouse_y != prev_my);

		int act = -1;  // a triggered tool button, performed after input decoding

		if (mouse_scroll != 0)
		{
			selected -= mouse_scroll;
			selected = selected < 0 ? 0 : (selected >= SES_NAV_COUNT ? SES_NAV_COUNT - 1 : selected);
			canvasFocus = false;
			mouse_scroll = 0;
		}

		const bool overCanvas = mouse_x >= CANV_X && mouse_x < CANV_X + SE_FRAME_W * CANV_SCALE &&
		                        mouse_y >= CANV_Y && mouse_y < CANV_Y + SE_FRAME_H * CANV_SCALE;

		// Do not join strokes across a release or the canvas edge.
		if (!mousedown || !overCanvas)
			strokeX = strokeY = -1;

		int hover = -1;
		if (mouse_x >= panX0 && mouse_x <= panX1)
		{
			if (mouse_y >= fieldsTop - 1 && mouse_y < fieldsTop + SES_ROW_COUNT * row_h)
				hover = (mouse_y - (fieldsTop - 1)) / row_h;
			else if (mouse_y >= actionsTop - 1 && mouse_y < actionsTop + ((SES_ACT_COUNT + 1) / 2) * act_h)
			{
				int a = ((mouse_y - (actionsTop - 1)) / act_h) * 2;
				if (a + 1 < SES_ACT_COUNT && mouse_x >= panMidX)
					a += 1;
				if (a >= SES_ACT_COUNT)
					a = SES_ACT_COUNT - 1;
				hover = SES_ROW_COUNT + a;
			}
		}
		if (hover >= 0 && (mouse_x != prev_mx || mouse_y != prev_my) && hover != selected)
		{
			JE_playSampleNum(S_CURSOR);
			selected = hover;
		}
		prev_mx = mouse_x;
		prev_my = mouse_y;

		if (newmouse || (mousedown && mouseMoved))
		{
			if (newmouse)
			{
				notice[0] = '\0';
				heldColor = (lastmouse_but == SDL_BUTTON_RIGHT || tool == SES_TOOL_ERASE) ? 0 : (JE_byte)color;
			}

			if (overCanvas)
			{
				const int px = (mouse_x - CANV_X) / CANV_SCALE;
				const int py = (mouse_y - CANV_Y) / CANV_SCALE;
				if (newmouse && lastmouse_but == SDL_BUTTON_MIDDLE)
					color = *seFramePx(bank, frame, px, py);
				else if (newmouse && lastmouse_but == SDL_BUTTON_RIGHT)
					*seFramePx(bank, frame, px, py) = 0;
				else if (newmouse)
					seApplyTool(bank, frame, px, py, &color, &tool);
				else if (tool == SES_TOOL_PAINT || tool == SES_TOOL_ERASE)
				{
					if (strokeX >= 0)
						seStrokeTo(bank, frame, strokeX, strokeY, px, py, heldColor);
					else
						*seFramePx(bank, frame, px, py) = heldColor;
				}
				strokeX = px;
				strokeY = py;
			}
			else if (newmouse)
			{
				if (lastmouse_but == SDL_BUTTON_RIGHT)
					done = true;
				else if (mouse_x >= PAL_X && mouse_x < PAL_X + 16 * PAL_CELL &&
				         mouse_y >= PAL_Y && mouse_y < PAL_Y + 16 * PAL_CELL)
				{
					color = (mouse_y - PAL_Y) / PAL_CELL * 16 + (mouse_x - PAL_X) / PAL_CELL;
					JE_playSampleNum(S_CURSOR);
				}
				else if (mouse_y >= STRIP_Y && mouse_y < STRIP_Y + SE_FRAME_H &&
				         mouse_x >= STRIP_X && mouse_x < STRIP_X + SE_FRAMES * 26)
				{
					frame = (mouse_x - STRIP_X) / 26 + 1;
					JE_playSampleNum(S_CURSOR);
				}
				else if (hover >= 0)
				{
					selected = hover;
					canvasFocus = false;
					if (hover < SES_ROW_COUNT)
					{
						const int dir = (mouse_x < panMidX) ? -1 : 1;
						switch (hover)
						{
						case SES_ROW_BANK:   bank = (bank + 7 + dir) % 8 + 1; break;
						case SES_ROW_FRAME:  frame = (frame + 4 + dir) % 5 + 1; break;
						case SES_ROW_TOOL:   tool = (tool + SES_TOOL_COUNT + dir) % SES_TOOL_COUNT; break;
						case SES_ROW_COLOR:  color = (color + 256 + dir) % 256; break;
						default:             source = (source + 5 + dir) % 6 + 1; break;
						}
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
				color = (color + 255) % 256;
				break;
			case SDL_SCANCODE_RIGHTBRACKET:
				color = (color + 1) % 256;
				break;
			case SDL_SCANCODE_H:
				act = SES_ACT_FLIP_H;
				break;
			case SDL_SCANCODE_V:
				act = SES_ACT_FLIP_V;
				break;
			case SDL_SCANCODE_BACKSPACE:
				if (canvasFocus)
					*seFramePx(bank, frame, curX, curY) = 0;
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_SPACE:
				if (canvasFocus)
					seApplyTool(bank, frame, curX, curY, &color, &tool);
				else if (selected >= SES_ROW_COUNT)
					act = selected;
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
				case SES_ROW_TOOL:   tool = (tool + SES_TOOL_COUNT + dir) % SES_TOOL_COUNT; break;
				case SES_ROW_COLOR:  color = (color + 256 + ((lastkey_mod & KMOD_SHIFT) ? dir * 16 : dir)) % 256; break;
				case SES_ROW_SOURCE: source = (source + 5 + dir) % 6 + 1; break;
				default:             break;
				}
				JE_playSampleNum(S_CURSOR);
			}
			newkey = false;
		}

		switch (act)
		{
		case SES_ACT_CAPTURE:
			seCaptureBank(bank, source);
			SDL_strlcpy(notice, "Captured", sizeof(notice));
			JE_playSampleNum(S_SELECT);
			break;
		case SES_ACT_COPY:
			for (int y = 0; y < SE_FRAME_H; ++y)
				for (int x = 0; x < SE_FRAME_W; ++x)
					*seFramePx(bank, frame, x, y) = *seFramePx(bank, 3, x, y);
			JE_playSampleNum(S_SELECT);
			break;
		case SES_ACT_FLIP_H:
		case SES_ACT_FLIP_V:
			seFlipFrame(bank, frame, act == SES_ACT_FLIP_V);
			JE_playSampleNum(S_SELECT);
			break;
		case SES_ACT_CLEAR:
			for (int y = 0; y < SE_FRAME_H; ++y)
				for (int x = 0; x < SE_FRAME_W; ++x)
					*seFramePx(bank, frame, x, y) = 0;
			JE_playSampleNum(S_SELECT);
			break;
		case SES_ACT_REVERT:
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

	// Loadout previews read the compiled blob.
	seRebuildShapes();

	wait_noinput(false, false, true);
	newkey = newmouse = false;
}

void JE_shipEditor(void)
{
	// The title screen has not necessarily loaded item data or shop sprites.
	if (weaponPort[1].name[0] == '\0')
		JE_loadItemDat();
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');

	if (!extraAvail)
		seSeedDefaults();

	// Normalize hand-edited or incompatible fields before cycling them.
	for (int slot = 1; slot <= 10; ++slot)
		for (int row = SE_ROW_GRAPHIC; row < SE_ROW_COUNT; ++row)
			if (!seValueOk(row, *seField(slot, row)))
				seStepField(slot, row, 1);

	JE_ShipsType savedTable;
	memcpy(savedTable, extraShips, sizeof(savedTable));

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

	enum { BOX_X0 = 8, BOX_Y0 = 8, BOX_X1 = 143, BOX_Y1 = 182 };
	const int panX0 = 150, panX1 = 313, panY0 = 7, panY1 = 183;
	const int fieldsTop = panY0 + 16;
	const int row_h = 12;
	const int actionsTop = panY1 - 12;
	const int panMidX = (panX0 + panX1) / 2;
	const int labelX = panX0 + 5, valueX = panX1 - 5;
	const int boxMid = (BOX_X0 + BOX_X1) / 2;
	enum { C_PANEL = 0xF1, C_DIV = 0xF6, C_HI = 0xFB, C_SEL = 0xF5 };

	int slot = 1;
	int selected = 0;
	char notice[40] = "";
	int prev_mx = mouse_x, prev_my = mouse_y;
	bool done = false;

	wait_noinput(false, false, true);
	newkey = newmouse = false;

	while (!done)
	{
		setDelay(3);

		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		Sprite2_array *sheet = &spriteSheet9;
		const JE_word gr = JE_SGr(0, slot, &sheet);  // the editor always edits the local file

		fill_rectangle_xy(VGAScreen, BOX_X0, BOX_Y0, BOX_X1, BOX_Y1, 0);
		JE_rectangle(VGAScreen, BOX_X0 - 1, BOX_Y0 - 1, BOX_X1 + 1, BOX_Y1 + 1, C_HI);

		char caption[16];
		snprintf(caption, sizeof(caption), "Tab + %d", slot % 10);
		draw_font_hv_shadow(VGAScreen, boxMid, BOX_Y0 + 5, caption, small_font, centered, 15, 4, false, 1);

		if (gr > 1)
		{
			for (int b = -2; b <= 2; ++b)
				blit_sprite2x2(VGAScreen, BOX_X0 + 3 + (b + 2) * 26, BOX_Y0 + 22, *sheet, gr + b * 2);
		}
		else
			seDrawHull(boxMid, BOX_Y0 + 22, sheet, gr);

		JE_drawItem(6, *seField(slot, SE_ROW_LEFT), BOX_X0 + 8, BOX_Y0 + 64);
		seDrawHull(boxMid, BOX_Y0 + 64, sheet, gr);
		JE_drawItem(7, *seField(slot, SE_ROW_RIGHT), BOX_X1 - 31, BOX_Y0 + 64);
		draw_font_hv_shadow(VGAScreen, BOX_X0 + 20, BOX_Y0 + 94, "SIDE L", small_font, centered, 15, 1, false, 1);
		draw_font_hv_shadow(VGAScreen, BOX_X1 - 19, BOX_Y0 + 94, "SIDE R", small_font, centered, 15, 1, false, 1);

		{
			// Type 0 uses the composed special icon.
			static const struct { JE_byte type; int row; const char *tag; } icons[5] = {
				{ 2, SE_ROW_FRONT, "FRONT" }, { 3, SE_ROW_REAR, "REAR" }, { 0, SE_ROW_SPECIAL, "SPEC" },
				{ 5, SE_ROW_GENERATOR, "POWER" }, { 4, SE_ROW_SHIELD, "SHLD" },
			};
			for (int i = 0; i < 5; ++i)
			{
				const int x = BOX_X0 + 2 + i * 27;
				const JE_byte v = *seField(slot, icons[i].row);
				if (icons[i].type == 0)
				{
					if (v != 0 && debug_special_is_safe(v))
						draw_special_icon(VGAScreen, x, BOX_Y0 + 108, v);
				}
				else
				{
					JE_drawItem(icons[i].type,
					            extraShipResolvePort((uint)customWeaponLocalOwner(), v),
					            x, BOX_Y0 + 108);
				}
				draw_font_hv_shadow(VGAScreen, x + 12, BOX_Y0 + 140, icons[i].tag, small_font, centered, 15, 1, false, 1);
			}
		}

		fill_rectangle_xy(VGAScreen, panX0, panY0, panX1, panY1, C_PANEL);
		JE_rectangle(VGAScreen, panX0, panY0, panX1, panY1, C_HI);
		draw_font_hv_shadow(VGAScreen, panX0 + 5, panY0 + 2, "SHIP EDITOR", small_font, left_aligned, 15, 3, false, 1);
		snprintf(caption, sizeof(caption), "SHIP %d", slot);
		draw_font_hv_shadow(VGAScreen, panX1 - 5, panY0 + 2, caption, small_font, right_aligned, 15, 3, false, 1);
		fill_rectangle_xy(VGAScreen, panX0 + 2, panY0 + 11, panX1 - 2, panY0 + 11, C_DIV);

		for (int r = 0; r < SE_ROW_COUNT; ++r)
		{
			const int ry = fieldsTop + r * row_h;
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

		fill_rectangle_xy(VGAScreen, panX0 + 2, actionsTop - 3, panX1 - 2, actionsTop - 3, C_DIV);
		const int actionWidth = (panX1 - panX0 - 4) / SE_ACT_COUNT;
		for (int a = 0; a < SE_ACT_COUNT; ++a)
		{
			const int bx0 = panX0 + 2 + a * actionWidth;
			const int bx1 = (a == SE_ACT_COUNT - 1) ? panX1 - 2 : bx0 + actionWidth - 2;
			const bool sel = (selected == SE_ROW_COUNT + a);
			fill_rectangle_xy(VGAScreen, bx0, actionsTop - 1, bx1, actionsTop + 9, sel ? C_SEL : C_DIV);
			draw_font_hv_shadow(VGAScreen, (bx0 + bx1) / 2, actionsTop, seActs[a].label, small_font, centered,
			                    15, sel ? 6 : 4, false, 1);
		}

		draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, vga_height - 12,
		                    notice[0] != '\0' ? notice
		                    : (selected < SE_ROW_COUNT ? seRows[selected].help : seActs[selected - SE_ROW_COUNT].help),
		                    small_font, centered, 15, 2, false, 1);

		touch_ui_set_layout(TOUCH_LAYOUT_LIST);

		push_joysticks_as_keyboard();
		service_SDL_events(false);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		seWaitTick();

		// Do not leave with unsaved changes.
		bool leave = false;
		bool revert = false;
		bool openSprites = false;

		if (mouse_scroll != 0)
		{
			selected -= mouse_scroll;
			selected = selected < 0 ? 0 : (selected >= SE_NAV_COUNT ? SE_NAV_COUNT - 1 : selected);
			mouse_scroll = 0;
		}

		int hover = -1;
		if (mouse_x >= panX0 && mouse_x <= panX1)
		{
			if (mouse_y >= fieldsTop - 1 && mouse_y < fieldsTop + SE_ROW_COUNT * row_h)
				hover = (mouse_y - (fieldsTop - 1)) / row_h;
			else if (mouse_y >= actionsTop - 1 && mouse_y <= actionsTop + 9)
			{
				int a = (mouse_x - (panX0 + 2)) / actionWidth;
				if (a >= SE_ACT_COUNT)
					a = SE_ACT_COUNT - 1;
				hover = SE_ROW_COUNT + (a < 0 ? 0 : a);
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
			if (lastmouse_but == SDL_BUTTON_RIGHT)
				leave = true;
			else if (hover >= 0 && hover < SE_NAV_COUNT)
			{
				selected = hover;
				if (hover == SE_ROW_SLOT)
				{
					slot += (mouse_x < panMidX) ? (slot > 1 ? -1 : 9) : (slot < 10 ? 1 : -9);
					JE_playSampleNum(S_CURSOR);
				}
				else if (hover < SE_ROW_COUNT)
				{
					seStepField(slot, hover, (mouse_x < panMidX) ? -1 : 1);
					JE_playSampleNum(S_CURSOR);
				}
				else if (hover == SE_ACT_SPRITES)
					openSprites = true;
				else if (hover == SE_ACT_REVERT)
					revert = true;
				else
					leave = true;
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
				if (selected == SE_ACT_SPRITES)
					openSprites = true;
				else if (selected == SE_ACT_REVERT)
					revert = true;
				else if (selected == SE_ACT_DONE)
					leave = true;
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
				if (selected == SE_ROW_SLOT)
					slot += (dir < 0) ? (slot > 1 ? -1 : 9) : (slot < 10 ? 1 : -9);
				else if (selected < SE_ROW_COUNT)
				{
					// Shift steps armor by ten.
					const int reps = (selected == SE_ROW_ARMOR && (lastkey_mod & KMOD_SHIFT)) ? 10 : 1;
					for (int i = 0; i < reps; ++i)
						seStepField(slot, selected, dir);
				}
				else
				{
					selected += dir;
					selected = selected < SE_ACT_SPRITES ? SE_ACT_SPRITES
					         : (selected > SE_ACT_DONE ? SE_ACT_DONE : selected);
				}
				JE_playSampleNum(S_CURSOR);
			}
			newkey = false;
		}

		if (openSprites)
		{
			const JE_byte grByte = *seField(slot, SE_ROW_GRAPHIC);
			seSpriteEditor(grByte > 7 ? grByte - 7 : 1);
		}

		if (revert)
		{
			memcpy(extraShips, savedTable, sizeof(savedTable));
			memcpy(seCells, seCellsSaved, sizeof(seCells));
			seRebuildShapes();
			SDL_strlcpy(notice, "Reverted", sizeof(notice));
			JE_playSampleNum(S_SELECT);
		}

		if (leave)
		{
			const bool tableChanged = memcmp(savedTable, extraShips, sizeof(savedTable)) != 0;
			const bool cellsChanged = memcmp(seCellsSaved, seCells, sizeof(seCells)) != 0;
			if (!tableChanged && !cellsChanged)
				done = true;
			else
			{
				// Custom graphics require a sprite blob, even when blank.
				if (extraShapes.size == 0)
					for (int s = 1; s <= 10; ++s)
						if (*seField(s, SE_ROW_GRAPHIC) > 7)
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
					SDL_strlcpy(notice, "Could not write newsh$.shp", sizeof(notice));
					JE_playSampleNum(S_SPRING);
				}
			}
		}
	}

	touch_ui_clear_layout();
	wait_noinput(false, false, true);

	VGAScreen = temp_surface;
	set_menu_centered(prevCentered);
	memcpy(colors, savedPalette, sizeof(Palette));  // restore the caller's palette
	set_palette(colors, 0, 255);
}
