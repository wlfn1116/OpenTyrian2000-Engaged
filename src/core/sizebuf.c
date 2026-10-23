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

/* Endian-aware bounded buffer access adapted from Quake-style size buffers.
 * An error latches for all later operations because animation decoding aborts
 * the complete record after any failed read or write. */
#include "sizebuf.h"

#include "SDL_endian.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Construct buffer with the passed array and size */
void SZ_Init(sizebuf_t * sz, Uint8 * buf, unsigned int size)
{
	sz->data = buf;
	sz->bufferLen = size;
	sz->bufferPos = 0;
	sz->error = false;
}

/* Check error flags */
bool SZ_Error(sizebuf_t * sz)
{
	return sz->error;
}

/* mimic memset */
void SZ_Memset(sizebuf_t * sz, int value, size_t count)
{
	/* Do bounds checking before writing */
	if (sz->error || sz->bufferPos + count > sz->bufferLen)
	{
		sz->error = true;
		return;
	}

	/* Memset and increment pointer */
	memset(sz->data + sz->bufferPos, value, count);
	sz->bufferPos += count;
}

/* Mimic memcpy. */
void SZ_Memcpy2(sizebuf_t * sz, sizebuf_t * bf, size_t count)
{
	/* State checking */
	if (sz->error || sz->bufferPos + count > sz->bufferLen)
	{
		sz->error = true;
		return;
	}
	if (bf->error || bf->bufferPos + count > bf->bufferLen)
	{
		bf->error = true;
		return;
	}

	/* Memcpy & increment */
	memcpy(sz->data + sz->bufferPos, bf->data + bf->bufferPos, count);
	sz->bufferPos += count;
	bf->bufferPos += count;
}

/* Reposition buffer pointer */
void SZ_Seek(sizebuf_t * sz, long count, int mode)
{
	/* Okay, it's reasonable to reset the error bool on seeking... */

	switch (mode)
	{
		case SEEK_SET:
			sz->bufferPos = count;
			break;
		case SEEK_CUR:
			sz->bufferPos += count;
			break;
		case SEEK_END:
			sz->bufferPos = sz->bufferLen - count;
			break;
		default:
			assert(false);
	}

	/* Check errors */
	if (sz->bufferPos > sz->bufferLen)
		sz->error = true;
	else
		sz->error = false;
}

/* Integer access stays centralized so bounds and endian handling remain paired. */
unsigned int MSG_ReadByte(sizebuf_t * sz)
{
	unsigned int ret;

	if (sz->error || sz->bufferPos + 1 > sz->bufferLen)
	{
		sz->error = true;
		return 0;
	}

	ret = sz->data[sz->bufferPos];
	sz->bufferPos += 1;

	return ret;
}

unsigned int MSG_ReadWord(sizebuf_t * sz)
{
	unsigned int ret;

	if (sz->error || sz->bufferPos + 2 > sz->bufferLen)
	{
		sz->error = true;
		return 0;
	}

	ret = SDL_SwapLE16(*((Uint16 *)(sz->data + sz->bufferPos)));
	sz->bufferPos += 2;

	return ret;
}
