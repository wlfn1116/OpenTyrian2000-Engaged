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
#include "animlib.h"

#include "crashlog.h"
#include "file.h"
#include "keyboard.h"
#include "network.h"
#include "nortsong.h"
#include "palette.h"
#include "sizebuf.h"
#include "video.h"

#include <assert.h>
#include <string.h>

/* Playback accepts only the fixed 320x200x8 format and the engine's frame rate.
 * Files whose retained header fields violate those assumptions are rejected. */
#define PALETTE_OFFSET    0x100 // 128 + sizeof(header)
#define PAGEHEADER_OFFSET 0x500 // PALETTE_OFFSET + sizeof(palette)
#define ANIM_OFFSET   0x0B00    // PAGEHEADER_OFFSET + sizeof(largepageheader) * 256
#define ANI_PAGE_SIZE 0x10000   // 65536.

typedef struct anim_FileHeader_s
{
	Uint16 nlps;            /* Page count, at most 256. */
	Uint32 nRecords;        /* Record count, at most 65535. */
} anim_FileHeader_t;

typedef struct anim_LargePageHeader_s
{
	Uint16 baseRecord;      /* First record number. */
	Uint16 nRecords;        /* Record count. */
	Uint16 nBytes;	        /* Data bytes excluding headers. */
} anim_LargePageHeader_t;

Uint8 CurrentPageBuffer[65536];
anim_LargePageHeader_t PageHeader[256];
Uint16 CurrentPageRecordSizes[256];

anim_LargePageHeader_t CurrentPageHeader;
anim_FileHeader_t FileHeader;

unsigned int Curlpnum;

FILE * InFile;

static Uint8 AnimScreen[320 * 200];

int JE_playRunSkipDump(Uint8 *, unsigned int);
void JE_closeAnim(void);
int JE_loadAnim(const char *);
int JE_renderFrame(unsigned int);
int JE_findPage (unsigned int);
int JE_loadPage(unsigned int);

/* Returns zero on success and a nonzero value for invalid page data. */
int JE_loadPage(unsigned int pagenumber)
{
	unsigned int i, pageSize;

	if (Curlpnum == pagenumber)
		return 0;
	Curlpnum = pagenumber;

	/* Each 0x10000-byte page repeats its header, followed by two padding bytes,
	 * one size word per record, and the compressed record data. */
	fseek(InFile, ANIM_OFFSET + (pagenumber * ANI_PAGE_SIZE), SEEK_SET);
	fread_u16_die(&CurrentPageHeader.baseRecord, 1, InFile);
	fread_u16_die(&CurrentPageHeader.nRecords,   1, InFile);
	fread_u16_die(&CurrentPageHeader.nBytes,     1, InFile);

	fseek(InFile, 2, SEEK_CUR);
	fread_u16_die(CurrentPageRecordSizes, CurrentPageHeader.nRecords, InFile);

	fread_die(CurrentPageBuffer, 1, CurrentPageHeader.nBytes, InFile);

	/* The record-size table must account for the complete compressed payload. */
	pageSize = 0;
	for (i = 0; i < CurrentPageHeader.nRecords; i++)
		pageSize += CurrentPageRecordSizes[i];

	if (pageSize != CurrentPageHeader.nBytes)
		return -1;

	return 0;
}

int JE_findPage(unsigned int framenumber)
{
	unsigned int i;

	for (i = 0; i < FileHeader.nlps; i++)
	{
		if (PageHeader[i].baseRecord <= framenumber &&
		    PageHeader[i].baseRecord + PageHeader[i].nRecords > framenumber)
		{
			return i;
		}
	}

	return -1;
}

int JE_renderFrame(unsigned int framenumber)
{
	unsigned int i, offset, destframe;

	destframe = framenumber - CurrentPageHeader.baseRecord;

	offset = 0;
	for (i = 0; i < destframe; i++)
		offset += CurrentPageRecordSizes[i];

	return (JE_playRunSkipDump(CurrentPageBuffer + offset + 4, CurrentPageRecordSizes[destframe] - 4));
}

void JE_playAnim(const char *animfile, JE_byte startingframe, JE_byte speed)
{
	unsigned int i;
	int pageNum;

	crashlog_set_phase("cutscene / animation");

	if (JE_loadAnim(animfile) != 0)
		return;

	set_menu_centered(true);
	memset(AnimScreen, 0, sizeof(AnimScreen));

	JE_clr256(VGAScreen);
	JE_showVGA();

	/* The final record is a delta back to the first frame and is omitted, matching
	 * the original playback loop. */
	for (i = startingframe; i < FileHeader.nRecords-1; i++)
	{
		setDelay(speed);

		pageNum = JE_findPage(i);
		if (pageNum == -1)
			break;
		if (JE_loadPage(pageNum) != 0)
			break;

		if (JE_renderFrame(i) != 0)
			break;
		JE_showVGA();

		service_SDL_events(true);
		if (newkey)
			break;

		NETWORK_KEEP_ALIVE();
		wait_delay();
	}

	JE_closeAnim();
}

/* Closes the input file before returning from any validation failure. */
int JE_loadAnim(const char *filename)
{
	unsigned int i;
	long fileSize;
	char temp[4];

	Curlpnum = -1;
	InFile = dir_fopen(data_dir(), filename, "rb");
	if (InFile == NULL)
		return -1;

	fileSize = ftell_eof(InFile);
	if (fileSize < ANIM_OFFSET)
	{
		/* The fixed header and page table end at ANIM_OFFSET. */
		fclose(InFile);
		return -1;
	}

	/* Only the signature, page count, and record count vary in the retained header. */
	fread_die(&temp, 1, 4, InFile);
	fseek(InFile, 2, SEEK_CUR);
	fread_u16_die(&FileHeader.nlps,     1, InFile);
	fread_u32_die(&FileHeader.nRecords, 1, InFile);

	if (memcmp(temp, "LPF ", 4) != 0 ||
	    FileHeader.nlps == 0  || FileHeader.nRecords == 0 ||
	    FileHeader.nlps > 256 || FileHeader.nRecords > 65535)
	{
		fclose(InFile);
		return -1;
	}

	fseek(InFile, PAGEHEADER_OFFSET, SEEK_SET);
	anim_LargePageHeader_t *lastPageHeader = NULL;
	for (i = 0; i < FileHeader.nlps; i++)
	{
		fread_u16_die(&PageHeader[i].baseRecord, 1, InFile);
		fread_u16_die(&PageHeader[i].nRecords,   1, InFile);
		fread_u16_die(&PageHeader[i].nBytes,     1, InFile);
		lastPageHeader = &PageHeader[i];
	}

	/* Trailing padding is permitted, but every declared page must fit. */
	if (lastPageHeader == NULL)
	{
		fclose(InFile);
		return -1;
	}
	const size_t required_size = ((size_t)FileHeader.nlps - 1) * ANI_PAGE_SIZE + ANIM_OFFSET + lastPageHeader->nBytes +
	                             lastPageHeader->nRecords * 2 + 8;
	if ((size_t)fileSize < required_size)
	{
		fclose(InFile);
		return -1;
	}

	fseek(InFile, PALETTE_OFFSET, SEEK_SET);
	for (i = 0; i < 256; i++)
	{
		Uint8 bgru[4];
		fread_u8_die(bgru, 4, InFile);
		colors[i].b = bgru[0];
		colors[i].g = bgru[1];
		colors[i].r = bgru[2];
	}
	set_palette(colors, 0, 255);

	return 0;
}

void JE_closeAnim(void)
{
	fclose(InFile);
}

/* RunSkipDump supports byte and word forms of run, skip, and copy, plus the
 * 0x80 0x00 0x00 stop marker. Skip preserves destination bytes. A nonzero
 * result marks invalid input and terminates playback. */
int JE_playRunSkipDump(Uint8 *incomingBuffer, unsigned int IncomingBufferLength)
{
	sizebuf_t Buffer_IN, Buffer_OUT;
	sizebuf_t * pBuffer_IN = &Buffer_IN, * pBuffer_OUT = &Buffer_OUT;

	#define ANI_SHORT_RLE  0x00
	#define ANI_SHORT_SKIP 0x80
	#define ANI_LONG_OP    0x80
	#define ANI_LONG_COPY_OR_RLE  0x8000
	#define ANI_LONG_RLE   0x4000
	#define ANI_STOP       0x0000

	SZ_Init(pBuffer_IN, incomingBuffer, IncomingBufferLength);
	SZ_Init(pBuffer_OUT, AnimScreen, sizeof(AnimScreen));

	/* 320x200 is the only supported animation format. */
	assert(sizeof(AnimScreen) == 320 * 200);

	while (true)
	{
		unsigned int opcode = MSG_ReadByte(pBuffer_IN);

		/* Size-buffer operations latch errors, so the next opcode boundary catches
		 * any failed read or write before more output is interpreted. */
		if (SZ_Error(pBuffer_IN) || SZ_Error(pBuffer_OUT))
			return -1;

		if (opcode == ANI_LONG_OP) /* Long operation. */
		{
			opcode = MSG_ReadWord(pBuffer_IN);

			if (opcode == ANI_STOP)
			{
				break;
			}
			else if (!(opcode & ANI_LONG_COPY_OR_RLE)) /* If it's not those two, it's a skip */
			{
				unsigned int count = opcode;
				SZ_Seek(pBuffer_OUT, count, SEEK_CUR);
			}
			else /* Now things get a bit more interesting... */
			{
				opcode &= ~ANI_LONG_COPY_OR_RLE; /* Clear that flag */

				if (opcode & ANI_LONG_RLE) /* RLE */
				{
					unsigned int count = opcode & ~ANI_LONG_RLE; /* Clear flag */

					/* Extract another byte */
					unsigned int value = MSG_ReadByte(pBuffer_IN);

					/* The actual run */
					SZ_Memset(pBuffer_OUT, value, count);
				}
				else
				{ /* Long copy */
					unsigned int count = opcode;

					/* Copy */
					SZ_Memcpy2(pBuffer_OUT, pBuffer_IN, count);
				}
			}
		} /* End of long ops */
		else /* short ops */
		{
			if (opcode & ANI_SHORT_SKIP) /* Short skip, move pointer only */
			{
				unsigned int count = opcode & ~ANI_SHORT_SKIP; /* clear flag to get count */
				SZ_Seek(pBuffer_OUT, count, SEEK_CUR);
			}
			else if (opcode == ANI_SHORT_RLE) /* Short RLE, memset the destination */
			{
				/* Extract a few more bytes */
				unsigned int count = MSG_ReadByte(pBuffer_IN);
				unsigned int value = MSG_ReadByte(pBuffer_IN);

				/* Run */
				SZ_Memset(pBuffer_OUT, value, count);
			}
			else /* Short copy, memcpy from src to dest. */
			{
				unsigned int count = opcode;

				/* Dump */
				SZ_Memcpy2(pBuffer_OUT, pBuffer_IN, count);
			}
		} /* End of short ops */
	}

	/* Copy to the main screen, accounting for pitch */
	for (int y = 0; y < 200; ++y)
	{
		memcpy((Uint8*)VGAScreen->pixels + y * VGAScreen->pitch,
			AnimScreen + y * 320, 320);
	}

	/* And that's that */
	return 0;
}
