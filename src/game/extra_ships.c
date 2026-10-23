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
#include "extra_ships.h"

#include "custom_weapon.h"
#include "episodes.h"
#include "file.h"
#include "game_state.h"
#include "lvlmast.h"
#include "network.h"
#include "opentyr.h"
#include "sprite.h"

#include "SDL.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define SAS (sizeof(JE_ShipsType) - 4)
#define EXTRA_SHIPS_USER_FILE "custom_ships.shp"

static const JE_byte extraCryptKey[10] = { 58, 23, 16, 192, 254, 82, 113, 147, 62, 99 };

JE_boolean extraAvail;
JE_ShipsType extraShips;
Sprite2_array extraShapes;
bool extraUserAvail;

static FILE *seOpenAsciiCaseFile(const char *dir, const char *canonical)
{
	char name[32];
	unsigned letterPositions[COUNTOF(name)];
	unsigned letterCount = 0;
	const size_t length = strlen(canonical);
	if (length >= sizeof(name))
		return NULL;

	memcpy(name, canonical, length + 1);
	for (unsigned i = 0; i < length; ++i)
	{
		if (name[i] >= 'A' && name[i] <= 'Z')
			name[i] += 'a' - 'A';
		if (name[i] >= 'a' && name[i] <= 'z')
			letterPositions[letterCount++] = i;
	}
	if (letterCount >= sizeof(unsigned) * CHAR_BIT)
		return NULL;

	for (unsigned mask = 0; mask < (1u << letterCount); ++mask)
	{
		for (unsigned i = 0; i < letterCount; ++i)
		{
			const unsigned pos = letterPositions[i];
			const char lower = canonical[pos] >= 'A' && canonical[pos] <= 'Z'
			                 ? canonical[pos] + ('a' - 'A') : canonical[pos];
			name[pos] = (mask & (1u << i)) != 0 ? lower - ('a' - 'A') : lower;
		}

		FILE *const f = dir_fopen(dir, name, "rb");
		if (f != NULL)
			return f;
	}

	return NULL;
}

static FILE *seOpenLegacyUserShapesIn(const char *dir)
{
	return seOpenAsciiCaseFile(dir, "user.shp");
}

bool JE_legacyUserShapeCaseSelfTest(void)
{
	FILE *const f = seOpenAsciiCaseFile(data_dir(), "USER1.SHP");
	if (f == NULL)
		return false;

	fclose(f);
	return true;
}

// Search writable state first, then data and executable directories.
FILE *extraShipsOpenLegacyUserShapes(const char **location)
{
	FILE *f = seOpenLegacyUserShapesIn(get_user_directory());
	if (f != NULL)
	{
		*location = "the save directory";
		return f;
	}

	f = seOpenLegacyUserShapesIn(data_dir());
	if (f != NULL)
	{
		*location = "the data directory";
		return f;
	}

	char *const base = SDL_GetBasePath();
	if (base != NULL)
	{
		char *const baseData = malloc_die(strlen(base) + sizeof("/data"));
		sprintf(baseData, "%s/data", base);
		f = seOpenLegacyUserShapesIn(baseData);
		free(baseData);
		if (f != NULL)
		{
			SDL_free(base);
			*location = "the executable's data directory";
			return f;
		}

		f = seOpenLegacyUserShapesIn(base);
		SDL_free(base);
		if (f != NULL)
		{
			*location = "beside the executable";
			return f;
		}
	}

	return NULL;
}

// Decrypt one table in place only when all four plaintext checksums match.
static bool seDecryptShips(JE_ShipsType ships)
{
	bool correct = true;
	JE_ShipsType s2;
	JE_byte y;

	for (int x = SAS - 1; x >= 0; x--)
	{
		// (unsigned) only to make the index's non-negativity local; x is >= 0 by the loop condition.
		const unsigned int k = (unsigned)(x + 1) % 10;
		OT_ASSUME(k < 10);
		s2[x] = ships[x] ^ extraCryptKey[k];
		if (x > 0)
			s2[x] ^= ships[x - 1];
	}  /*  <= Key Decryption Test (Reversed key) */

	y = 0;
	for (uint x = 0; x < SAS; x++)
		y += s2[x];
	if (ships[SAS + 0] != y)
		correct = false;

	y = 0;
	for (uint x = 0; x < SAS; x++)
		y -= s2[x];
	if (ships[SAS + 1] != y)
		correct = false;

	y = 1;
	for (uint x = 0; x < SAS; x++)
		y = y * s2[x] + 1;
	if (ships[SAS + 2] != y)
		correct = false;

	y = 0;
	for (uint x = 0; x < SAS; x++)
		y ^= s2[x];
	if (ships[SAS + 3] != y)
		correct = false;

	if (correct)
		memcpy(ships, s2, sizeof(JE_ShipsType));

	return correct;
}

JE_boolean JE_decryptShips(void) { return seDecryptShips(extraShips); }

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

// Read a compiled SHIPEDIT file without changing the active editor state on failure.
bool extraShipsReadCompiled(FILE *f, JE_ShipsType table, Sprite2_array *shapes)
{
	const long fileSize = ftell_eof(f);
	if (fileSize < (long)sizeof(JE_ShipsType) || fileSize - (long)sizeof(JE_ShipsType) > UINT16_MAX)
		return false;

	Sprite2_array loaded = {0, NULL};
	loaded.size = (size_t)(fileSize - (long)sizeof(JE_ShipsType));
	if (loaded.size > 0)
		loaded.data = malloc_die(loaded.size);

	rewind(f);
	const bool read = (loaded.size == 0 || fread(loaded.data, 1, loaded.size, f) == loaded.size) &&
	                  fread(table, 1, sizeof(JE_ShipsType), f) == sizeof(JE_ShipsType) &&
	                  fgetc(f) == EOF && !ferror(f);
	if (!read || !seDecryptShips(table))
	{
		free_sprite2s(&loaded);
		return false;
	}

	*shapes = loaded;
	return true;
}

bool JE_stockExtraShapesSelfTest(void)
{
	FILE *const f = dir_fopen(data_dir(), EXTRA_SHIPS_STOCK_FILE, "rb");
	if (f == NULL)
		return false;

	JE_ShipsType table;
	Sprite2_array shapes = {0, NULL};
	const bool ok = extraShipsReadCompiled(f, table, &shapes) && shapes.size > 0;
	fclose(f);
	free_sprite2s(&shapes);
	return ok;
}

void JE_loadExtraShapes(void)
{
	JE_freeExtraShapes();
	extraUserAvail = false;

	// User.shp is an editor import source, never an automatic startup override.
	// Load the saved custom file when present; otherwise use the untouched shipped defaults.
	FILE *f = dir_fopen(get_user_directory(), EXTRA_SHIPS_USER_FILE, "rb");
	bool fromUser = f != NULL;
	if (f == NULL)
		f = dir_fopen(data_dir(), EXTRA_SHIPS_STOCK_FILE, "rb");
	if (f == NULL)
		return;

	JE_ShipsType loadedTable;
	Sprite2_array loadedShapes = {0, NULL};
	if (!extraShipsReadCompiled(f, loadedTable, &loadedShapes))
	{
		fclose(f);
		if (!fromUser)
		{
			fprintf(stderr, "warning: stock custom ship file is invalid; ignoring it\n");
			return;
		}

		fprintf(stderr, "warning: %s is invalid; using the stock ships\n", EXTRA_SHIPS_USER_FILE);
		fromUser = false;
		f = dir_fopen(data_dir(), EXTRA_SHIPS_STOCK_FILE, "rb");
		if (f == NULL || !extraShipsReadCompiled(f, loadedTable, &loadedShapes))
		{
			if (f != NULL)
				fclose(f);
			return;
		}
	}
	fclose(f);

	memcpy(extraShips, loadedTable, sizeof(extraShips));
	extraShapes = loadedShapes;
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

// Preserve ShipEdit graphic IDs 1 through 15. See doc/notes.md#extra-ships.
static const JE_word extraLegacyGraphics[7] = { 233, 157, 195, 271, 81, 0, 119 };
static const JE_word extraCustomGraphics[8] = { 5, 43, 81, 119, 157, 195, 233, 271 };

bool extraShipGraphicIsCustom(int graphic)
{
	return graphic >= 8 && graphic <= 15;
}

static bool extraShipLegacyGraphic(JE_word raw)
{
	for (uint i = 0; i < COUNTOF(extraLegacyGraphics); ++i)
		if (extraLegacyGraphics[i] == raw)
			return true;
	return false;
}

// Sort raw graphics so extended IDs stay stable and shared artwork appears once.
static int extraShipExtendedGraphics(JE_word *graphics, size_t capacity)
{
	int count = 0;
	for (int ship = 1; ship <= SHIP_DRAGONWING; ++ship)
	{
		const JE_word raw = ships[ship].shipgraphic;
		if (ships[ship].name[0] == '\0' || extraShipLegacyGraphic(raw))
			continue;

		int at = 0;
		while (at < count && graphics[at] < raw)
			++at;
		if (at < count && graphics[at] == raw)
			continue;
		if ((size_t)count >= capacity)
			continue;

		memmove(&graphics[at + 1], &graphics[at], (size_t)(count - at) * sizeof(*graphics));
		graphics[at] = raw;
		++count;
	}
	return count;
}

static bool extraShipGraphicRaw(int graphic, JE_word *raw)
{
	if (graphic >= 1 && graphic <= 7)
	{
		*raw = extraLegacyGraphics[graphic - 1];
		return true;
	}
	if (graphic < 16)
		return false;

	JE_word extended[SHIP_DRAGONWING];
	const int count = extraShipExtendedGraphics(extended, COUNTOF(extended));
	const int index = graphic - 16;
	if (index < 0 || index >= count)
		return false;
	*raw = extended[index];
	return true;
}

bool extraShipGraphicIsNort(int graphic)
{
	JE_word raw;
	return extraShipGraphicRaw(graphic, &raw) && raw == 1;
}

int extraShipGraphicMax(void)
{
	JE_word extended[SHIP_DRAGONWING];
	return 15 + extraShipExtendedGraphics(extended, COUNTOF(extended));
}

const char *extraShipEditorGraphicName(int graphic)
{
	JE_word raw;
	if (!extraShipGraphicRaw(graphic, &raw))
		return NULL;

	for (int ship = 1; ship <= SHIP_DRAGONWING; ++ship)
		if (ships[ship].name[0] != '\0' && ships[ship].shipgraphic == raw)
			return ship == 1 ? "USP Talon" : ships[ship].name;
	return NULL;
}

// Route online extra-ship graphics through the owning seat's file.
JE_word JE_SGr(uint seat, JE_word ship, Sprite2_array **ptr)
{
	const int graphic = extraShipsFor(seat)[(ship - 1) * 15];
	if (extraShipGraphicIsCustom(graphic))
	{
		*ptr = extraShapesFor(seat);
		return extraCustomGraphics[graphic - 8];
	}

	JE_word raw;
	if (!extraShipGraphicRaw(graphic, &raw))
		raw = extraLegacyGraphics[0]; // invalid graphics fall back to the first built-in hull

	if (raw > 500)
	{
		*ptr = &spriteSheetT2000;
		return raw - 500;
	}
	*ptr = &spriteSheet9;
	return raw;
}

enum { EXTRA_SHIPS_WIRE_VERSION = 1 };

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

/* Install transferred local ships. Absence removes the receiver's copy and reloads stock. */
bool extraShipsAdoptLocal(const Uint8 *buf, size_t len)
{
	if (!extraShipsPayloadValid(buf, len))
		return false;
	if (buf[1] == 0)
	{
		if (!dir_remove_file(get_user_directory(), EXTRA_SHIPS_USER_FILE))
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
bool JE_saveExtraShapes(void)
{
	JE_ShipsType enc;
	JE_encryptShips(enc);

	FILE *f = dir_fopen(make_user_directory(), EXTRA_SHIPS_USER_FILE, "wb");
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

// Resolve the sentinel independently of the machine-local Weapon Creator toggle.
JE_byte extraShipResolvePort(uint seat, JE_byte port)
{
	if (port != EXTRA_SHIP_CUSTOM_PORT)
		return port;

	const int reserved = (seat < CUSTOM_WEAPON_OWNERS) ? customWeaponOwnerPort[seat] : 0;
	return (reserved > 0) ? (JE_byte)reserved : 0;
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

int extraShipPreviewBank(Uint32 elapsed)
{
	// Sweep from center to full left, through center to full right, then repeat.
	static const Sint8 banking[] = { 0, -1, -2, -1, 0, 1, 2, 1 };
	return banking[(elapsed / EXTRA_SHIP_PREVIEW_BANK_MS) % COUNTOF(banking)];
}
