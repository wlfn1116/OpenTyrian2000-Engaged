/*
 * Custom-episode container loading. The format is documented in doc/notes.md.
 *
 * This program is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later version.
 */
#include "custom_episode.h"

#include "config.h"    // get_user_directory
#include "episodes.h"  // JE_initEpisode, episodeNum, JE_forceEpisodeReload
#include "file.h"      // data_dir, malloc_die
#include "network.h"   // isNetworkGame
#include "lvllib.h"    // lvlNum, JE_levelFileCount
#include "qa.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#include <direct.h>
#define mkdir _mkdir
#define rmdir _rmdir
#else
#include <unistd.h>
#endif

#ifdef _WIN32
// Keep windows.h from dragging in the old winsock.h behind SDL_net's winsock2.h.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#endif

#define CLV_HEADER_SIZE 160
#define CLV_MAX_BYTES   CUSTOM_EPISODE_BYTES_MAX

typedef struct
{
	char file[CUSTOM_EPISODE_FILE_LEN];    // File name within custom_episode_dir().
	char title[CUSTOM_EPISODE_TITLE_LEN];
	char author[CUSTOM_EPISODE_TITLE_LEN];
	JE_byte base;
	Uint32 size;
} CustomEpisodeEntry;

typedef struct
{
	char title[CUSTOM_EPISODE_TITLE_LEN];
	char author[CUSTOM_EPISODE_TITLE_LEN];
	JE_byte base;
	Uint32 lvlOff, lvlLen, scrOff, scrLen, cubeOff, cubeLen;
} ClvHeader;

static CustomEpisodeEntry entries[CUSTOM_EPISODE_MAX];
static int entryCount = 0;
static unsigned int containerWrites = 0;

/* A rescan must not change the active episode. */
static bool activeFlag = false;
static bool activatingNow = false;
static char activeFile[CUSTOM_EPISODE_FILE_LEN] = "";
static char activeTitle[CUSTOM_EPISODE_TITLE_LEN] = "";

static Uint32 le32(const Uint8 *p)
{
	return (Uint32)p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16) | ((Uint32)p[3] << 24);
}

static void trim_trailing_spaces(char *s)
{
	size_t len = strlen(s);
	while (len > 0 && s[len - 1] == ' ')
		s[--len] = '\0';
}

static bool clv_parse_header(const Uint8 *h, Uint32 fileSize, ClvHeader *out)
{
	if (memcmp(h, "CLV1", 4) != 0)
		return false;

	memcpy(out->title, h + 4, CUSTOM_EPISODE_TITLE_LEN - 1);
	out->title[CUSTOM_EPISODE_TITLE_LEN - 1] = '\0';
	trim_trailing_spaces(out->title);
	memcpy(out->author, h + 68, CUSTOM_EPISODE_TITLE_LEN - 1);
	out->author[CUSTOM_EPISODE_TITLE_LEN - 1] = '\0';
	trim_trailing_spaces(out->author);

	out->base = h[132];
	if (out->base < 1 || out->base > 5)
		out->base = 1;

	out->lvlOff  = le32(h + 136);  out->lvlLen  = le32(h + 140);
	out->scrOff  = le32(h + 144);  out->scrLen  = le32(h + 148);
	out->cubeOff = le32(h + 152);  out->cubeLen = le32(h + 156);

	if (out->lvlLen == 0 || out->scrLen == 0)
		return false;
	if (out->lvlOff < CLV_HEADER_SIZE || (Uint64)out->lvlOff + out->lvlLen > fileSize)
		return false;
	if (out->scrOff < CLV_HEADER_SIZE || (Uint64)out->scrOff + out->scrLen > fileSize)
		return false;
	if (out->cubeLen != 0 &&
	    (out->cubeOff < CLV_HEADER_SIZE || (Uint64)out->cubeOff + out->cubeLen > fileSize))
		return false;
	return true;
}

/* Reject invalid level counts before the fatal engine loaders see them. */
static bool clv_lvl_looks_sane(FILE *f, const ClvHeader *h)
{
	Uint8 b[2];
	if (fseek(f, (long)h->lvlOff, SEEK_SET) != 0 || fread(b, 1, 2, f) != 2)
		return false;
	const unsigned int lvlNum = b[0] | (b[1] << 8);
	return lvlNum >= 3 && lvlNum < 43 && lvlNum % 2 == 1;
}

const char *custom_episode_dir(void)
{
	static char dir[500] = "";
	if (dir[0] == '\0')
		snprintf(dir, sizeof(dir), "%s/custom_levels", get_user_directory());
	return dir;
}

/* Do not create the directory until a write needs it. */
static void custom_episode_dir_ensure(void)
{
	// A later fopen reports any mkdir failure.
#ifndef TARGET_WIN32
	const int mkdir_user = mkdir(get_user_directory(), 0700);
	const int mkdir_result = mkdir(custom_episode_dir(), 0700);
#else
	const int mkdir_user = mkdir(get_user_directory());
	const int mkdir_result = mkdir(custom_episode_dir());
#endif
	(void)mkdir_user; (void)mkdir_result;
}

static bool has_clv_ext(const char *name)
{
	const size_t len = strlen(name);
	return len >= 5 && SDL_strcasecmp(name + len - 4, ".clv") == 0;
}

/* Lists plain *.clv file names. */
static int list_clv_files(const char *dir, char names[][CUSTOM_EPISODE_FILE_LEN], int maxNames)
{
	int n = 0;
	if (dir == NULL || dir[0] == '\0')
		return 0;

#ifdef _WIN32
	char pattern[600];
	snprintf(pattern, sizeof(pattern), "%s/*.clv", dir);

	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	do
	{
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		if (!has_clv_ext(fd.cFileName))  // "*.clv" also matches longer 8.3 short-name tails
			continue;
		if (strlen(fd.cFileName) >= CUSTOM_EPISODE_FILE_LEN)
		{
			fprintf(stderr, "custom episode: name too long, skipping '%s'\n", fd.cFileName);
			continue;
		}
		if (n < maxNames)
			SDL_strlcpy(names[n++], fd.cFileName, CUSTOM_EPISODE_FILE_LEN);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
#else
	DIR *d = opendir(dir);
	if (d == NULL)
		return 0;
	const struct dirent *e;
	while ((e = readdir(d)) != NULL)
	{
		if (!has_clv_ext(e->d_name))
			continue;
		if (strlen(e->d_name) >= CUSTOM_EPISODE_FILE_LEN)
		{
			fprintf(stderr, "custom episode: name too long, skipping '%s'\n", e->d_name);
			continue;
		}
		if (n < maxNames)
			SDL_strlcpy(names[n++], e->d_name, CUSTOM_EPISODE_FILE_LEN);
	}
	closedir(d);
#endif
	return n;
}

/* Moves a loose container, falling back to copy-and-delete across volumes. */
static void migrate_one(const char *dir, const char *name)
{
	char src[600], dst[600];
	snprintf(src, sizeof(src), "%s/%s", dir, name);
	snprintf(dst, sizeof(dst), "%s/%s", custom_episode_dir(), name);

	FILE *exists = fopen(dst, "rb");
	if (exists != NULL)
	{
		fclose(exists);
		fprintf(stderr, "custom episode: '%s' already exists, leaving '%s' where it is\n", dst, src);
		return;
	}

	custom_episode_dir_ensure();

	if (rename(src, dst) == 0)
	{
		fprintf(stderr, "custom episode: moved '%s' into %s\n", name, custom_episode_dir());
		return;
	}

	FILE *in = fopen(src, "rb");
	if (in == NULL)
		return;
	FILE *out = fopen(dst, "wb");
	if (out == NULL)
	{
		fclose(in);
		return;
	}
	char buf[65536];
	size_t got;
	bool ok = true;
	while ((got = fread(buf, 1, sizeof(buf), in)) > 0)
	{
		if (fwrite(buf, 1, got, out) != got)
		{
			ok = false;
			break;
		}
	}
	ok = ok && !ferror(in);
	fclose(in);
	ok = (fclose(out) == 0) && ok;
	if (ok)
	{
		remove(src);
		fprintf(stderr, "custom episode: copied '%s' into %s\n", name, custom_episode_dir());
	}
	else
	{
		remove(dst);  // Never leave a partial container for the scanner.
	}
}

static void migrate_strays_from(const char *dir)
{
	if (dir == NULL || dir[0] == '\0')
		return;
	char names[CUSTOM_EPISODE_MAX][CUSTOM_EPISODE_FILE_LEN];
	// Do not mutate the directory while enumerating it.
	const int n = list_clv_files(dir, names, CUSTOM_EPISODE_MAX);
	for (int i = 0; i < n; ++i)
		migrate_one(dir, names[i]);
}

static int entry_cmp(const void *a, const void *b)
{
	return SDL_strcasecmp(((const CustomEpisodeEntry *)a)->file,
	                      ((const CustomEpisodeEntry *)b)->file);
}

/* The menu probes this every frame. A zero timestamp invalidates the cache. */
static Uint32 anyPresentCheckedAt = 0;
static bool anyPresentCached = false;

/* Invalid containers still count so the Clear action remains available. */
bool customEpisodeAnyPresent(void)
{
	const Uint32 now = SDL_GetTicks();
	if (anyPresentCheckedAt != 0 && now - anyPresentCheckedAt < 1000)
		return anyPresentCached;

	char names[CUSTOM_EPISODE_MAX][CUSTOM_EPISODE_FILE_LEN];
	bool present = list_clv_files(custom_episode_dir(), names, CUSTOM_EPISODE_MAX) > 0 ||
	               list_clv_files(data_dir(), names, CUSTOM_EPISODE_MAX) > 0 ||
	               list_clv_files(get_user_directory(), names, CUSTOM_EPISODE_MAX) > 0;
	if (!present)
	{
		// Clear also removes an empty leftover directory.
		struct stat st;
		present = stat(custom_episode_dir(), &st) == 0;
	}

	anyPresentCached = present;
	anyPresentCheckedAt = now != 0 ? now : 1;
	return present;
}

void customEpisodeClearAll(void)
{
	if (activeFlag)
		customEpisodeDeactivate();

	const char *const dirs[] = { custom_episode_dir(), data_dir(), get_user_directory() };
	for (unsigned int d = 0; d < COUNTOF(dirs); ++d)
	{
		// Repeat because each pass is capped at CUSTOM_EPISODE_MAX names.
		for (int pass = 0; pass < 8; ++pass)
		{
			char names[CUSTOM_EPISODE_MAX][CUSTOM_EPISODE_FILE_LEN];
			const int n = list_clv_files(dirs[d], names, CUSTOM_EPISODE_MAX);
			if (n == 0)
				break;
			bool removedAny = false;
			for (int i = 0; i < n; ++i)
			{
				char path[600];
				snprintf(path, sizeof(path), "%s/%s", dirs[d], names[i]);
				if (remove(path) == 0)
				{
					removedAny = true;
					fprintf(stderr, "custom episode: deleted '%s'\n", path);
				}
				else
					fprintf(stderr, "custom episode: could not delete '%s'\n", path);
			}
			if (!removedAny)
				break;   // Do not spin on undeletable files.
		}
	}

	// rmdir preserves a directory containing unrelated files.
	static const char *const extracted[] =
		{ CUSTOM_EP_LVL_NAME, CUSTOM_EP_SCRIPT_NAME, CUSTOM_EP_CUBES_NAME };
	for (unsigned int i = 0; i < COUNTOF(extracted); ++i)
	{
		char path[600];
		snprintf(path, sizeof(path), "%s/%s", custom_episode_dir(), extracted[i]);
		remove(path);
	}
	if (rmdir(custom_episode_dir()) == 0)
		fprintf(stderr, "custom episode: removed '%s'\n", custom_episode_dir());

	entryCount = 0;
	anyPresentCheckedAt = 0;
	// Clearing the files also clears their Endless-pool setting.
	customEndlessMode = CUSTOM_ENDLESS_OFF;
	customEpisodeSessionEnd();
}

void customEpisodeScan(void)
{
	// Migrate containers beside the data or user directory before scanning.
	migrate_strays_from(data_dir());
	if (strcmp(data_dir(), get_user_directory()) != 0)
		migrate_strays_from(get_user_directory());

	entryCount = 0;

	char names[CUSTOM_EPISODE_MAX][CUSTOM_EPISODE_FILE_LEN];
	const int n = list_clv_files(custom_episode_dir(), names, CUSTOM_EPISODE_MAX);

	for (int i = 0; i < n && entryCount < CUSTOM_EPISODE_MAX; ++i)
	{
		char path[600];
		snprintf(path, sizeof(path), "%s/%s", custom_episode_dir(), names[i]);
		FILE *f = fopen(path, "rb");
		if (f == NULL)
			continue;

		fseek(f, 0, SEEK_END);
		const long size = ftell(f);
		fseek(f, 0, SEEK_SET);

		Uint8 hbuf[CLV_HEADER_SIZE];
		ClvHeader h;
		const bool ok = size >= CLV_HEADER_SIZE && size <= CLV_MAX_BYTES &&
		                fread(hbuf, 1, CLV_HEADER_SIZE, f) == CLV_HEADER_SIZE &&
		                clv_parse_header(hbuf, (Uint32)size, &h) &&
		                clv_lvl_looks_sane(f, &h);
		fclose(f);
		if (!ok)
		{
			fprintf(stderr, "custom episode: '%s' is not a loadable container, skipping\n", path);
			continue;
		}

		CustomEpisodeEntry *e = &entries[entryCount++];
		SDL_strlcpy(e->file, names[i], sizeof(e->file));
		SDL_strlcpy(e->title, h.title, sizeof(e->title));
		SDL_strlcpy(e->author, h.author, sizeof(e->author));
		e->base = h.base;
		e->size = (Uint32)size;

		if (e->title[0] == '\0')
		{
			// A blank title falls back to the file name without its extension.
			SDL_strlcpy(e->title, names[i], sizeof(e->title));
			e->title[strlen(e->title) - 4] = '\0';
		}
	}

	// Match Atlas ordering.
	qsort(entries, (size_t)entryCount, sizeof(entries[0]), entry_cmp);

	anyPresentCheckedAt = 0;
}

int customEpisodeCount(void)
{
	return entryCount;
}

const char *customEpisodeTitle(int index)
{
	return (index >= 0 && index < entryCount) ? entries[index].title : "";
}

const char *customEpisodeAuthor(int index)
{
	return (index >= 0 && index < entryCount) ? entries[index].author : "";
}

const char *customEpisodeFile(int index)
{
	return (index >= 0 && index < entryCount) ? entries[index].file : "";
}

JE_byte customEpisodeBase(int index)
{
	return (index >= 0 && index < entryCount) ? entries[index].base : 1;
}

Uint32 customEpisodeSize(int index)
{
	return (index >= 0 && index < entryCount) ? entries[index].size : 0;
}

int customEpisodeFindByFile(const char *fileName)
{
	if (fileName == NULL || fileName[0] == '\0')
		return -1;
	for (int i = 0; i < entryCount; ++i)
		if (SDL_strcasecmp(entries[i].file, fileName) == 0)
			return i;
	return -1;
}

/* Local Custom Endless setting; see doc/notes.md. */
int customEndlessMode = CUSTOM_ENDLESS_OFF;

/* Host-ordered collection used by both peers; -1 selects the local list. */
static char sessionNames[CUSTOM_EPISODE_MAX][CUSTOM_EPISODE_FILE_LEN];
static int  sessionCount = -1;
static int  sessionMode = CUSTOM_ENDLESS_OFF;

void customEpisodeSessionBegin(const char names[][CUSTOM_EPISODE_FILE_LEN], int count, int mode)
{
	if (count < 0)
		count = 0;
	if (count > CUSTOM_EPISODE_MAX)
		count = CUSTOM_EPISODE_MAX;
	for (int i = 0; i < count; ++i)
		SDL_strlcpy(sessionNames[i], names[i], CUSTOM_EPISODE_FILE_LEN);
	sessionCount = count;
	sessionMode = mode;
}

void customEpisodeSessionEnd(void)
{
	sessionCount = -1;
	sessionMode = CUSTOM_ENDLESS_OFF;
}

bool customEpisodeSessionActive(void)
{
	return sessionCount >= 0;
}

int customEpisodeIdCount(void)
{
	return sessionCount >= 0 ? sessionCount : entryCount;
}

int customEpisodeIdToLocal(int idIndex)
{
	if (sessionCount < 0)
		return (idIndex >= 0 && idIndex < entryCount) ? idIndex : -1;
	if (idIndex < 0 || idIndex >= sessionCount)
		return -1;
	return customEpisodeFindByFile(sessionNames[idIndex]);
}

int customEpisodeIdFromLocal(int localIndex)
{
	if (localIndex < 0 || localIndex >= entryCount)
		return -1;
	if (sessionCount < 0)
		return localIndex;
	for (int i = 0; i < sessionCount; ++i)
		if (SDL_strcasecmp(sessionNames[i], entries[localIndex].file) == 0)
			return i;
	return -1;
}

size_t customEpisodeCollectionString(char *out, size_t cap)
{
	if (cap == 0)
		return 0;
	out[0] = '\0';
	size_t at = 0;
	const int count = customEpisodeIdCount();
	for (int i = 0; i < count; ++i)
	{
		const int local = customEpisodeIdToLocal(i);
		const char *const name = local >= 0
		                       ? entries[local].file
		                       : (sessionCount >= 0 ? sessionNames[i] : "");
		if (name[0] == '\0')
			continue;
		const size_t len = strlen(name);
		if (at + len + 2 > cap)
			break;
		if (at > 0)
			out[at++] = ':';
		memcpy(&out[at], name, len);
		at += len;
		out[at] = '\0';
	}
	return at;
}

int customEpisodeCollectionNames(const char *joined,
                                 char names[][CUSTOM_EPISODE_FILE_LEN], int max)
{
	int count = 0;
	if (joined == NULL)
		return 0;
	const char *p = joined;
	while (*p != '\0' && count < max)
	{
		const char *const sep = strchr(p, ':');
		const size_t len = sep != NULL ? (size_t)(sep - p) : strlen(p);
		if (len > 0 && len < CUSTOM_EPISODE_FILE_LEN)
		{
			memcpy(names[count], p, len);
			names[count][len] = '\0';
			if (customEpisodeFileNameValid(names[count]))
				++count;
		}
		if (sep == NULL)
			break;
		p = sep + 1;
	}
	return count;
}

bool customEpisodeCollectionMissing(const char *joined)
{
	char names[CUSTOM_EPISODE_MAX][CUSTOM_EPISODE_FILE_LEN];
	const int count = customEpisodeCollectionNames(joined, names, CUSTOM_EPISODE_MAX);
	for (int i = 0; i < count; ++i)
		if (customEpisodeFindByFile(names[i]) < 0)
			return true;
	return false;
}

bool customEpisodeSaveDepsMissing(const char *epFile, const char *collection)
{
	if (epFile[0] != '\0' && customEpisodeFindByFile(epFile) < 0)
		return true;
	return collection[0] != '\0' && customEpisodeCollectionMissing(collection);
}

int customEndlessEffectiveMode(void)
{
	// A session list overrides local files and settings.
	if (sessionCount >= 0)
		return sessionCount > 0 &&
		       (sessionMode == CUSTOM_ENDLESS_MIXED || sessionMode == CUSTOM_ENDLESS_ONLY)
		     ? sessionMode : CUSTOM_ENDLESS_OFF;
	// Online play waits for a host session list before enabling custom levels.
	if (isNetworkGame || entryCount == 0)
		return CUSTOM_ENDLESS_OFF;
	return (customEndlessMode == CUSTOM_ENDLESS_MIXED || customEndlessMode == CUSTOM_ENDLESS_ONLY)
	     ? customEndlessMode : CUSTOM_ENDLESS_OFF;
}

/* Opens an inactive container at the start of its script section. */
FILE *customEpisodeOpenScript(int index, long *endOut)
{
	if (index < 0 || index >= entryCount)
		return NULL;

	char path[600];
	snprintf(path, sizeof(path), "%s/%s", custom_episode_dir(), entries[index].file);
	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return NULL;

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	Uint8 hbuf[CLV_HEADER_SIZE];
	ClvHeader h;
	if (size < CLV_HEADER_SIZE ||
	    fread(hbuf, 1, CLV_HEADER_SIZE, f) != CLV_HEADER_SIZE ||
	    !clv_parse_header(hbuf, (Uint32)size, &h) ||
	    fseek(f, (long)h.scrOff, SEEK_SET) != 0)
	{
		fclose(f);
		return NULL;
	}

	*endOut = (long)h.scrOff + (long)h.scrLen;
	return f;
}

unsigned int customEpisodeLevelCount(int index)
{
	if (index < 0 || index >= entryCount)
		return 0;

	char path[600];
	snprintf(path, sizeof(path), "%s/%s", custom_episode_dir(), entries[index].file);
	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return 0;

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	Uint8 hbuf[CLV_HEADER_SIZE];
	ClvHeader h;
	Uint8 b[2];
	unsigned int lvlNumWord = 0;
	if (size >= CLV_HEADER_SIZE &&
	    fread(hbuf, 1, CLV_HEADER_SIZE, f) == CLV_HEADER_SIZE &&
	    clv_parse_header(hbuf, (Uint32)size, &h) &&
	    fseek(f, (long)h.lvlOff, SEEK_SET) == 0 &&
	    fread(b, 1, 2, f) == 2)
	{
		lvlNumWord = b[0] | (b[1] << 8);
	}
	fclose(f);

	// Match the scanner's level-count validation.
	if (lvlNumWord < 3 || lvlNumWord >= 43 || lvlNumWord % 2 == 0)
		return 0;
	return lvlNumWord / 2;
}

bool customEpisodeActive(void)
{
	return activeFlag;
}

int customEpisodeCurrent(void)
{
	return activeFlag ? customEpisodeFindByFile(activeFile) : -1;
}

const char *customEpisodeActiveTitle(void)
{
	return activeFlag ? activeTitle : "";
}

const char *customEpisodeActiveFile(void)
{
	return activeFlag ? activeFile : "";
}

bool customEpisodeActivating(void)
{
	return activatingNow;
}

static bool extract_section(FILE *f, Uint32 off, Uint32 len, const char *name)
{
	char path[600];
	snprintf(path, sizeof(path), "%s/%s", custom_episode_dir(), name);
	custom_episode_dir_ensure();
	FILE *out = fopen(path, "wb");
	if (out == NULL)
	{
		fprintf(stderr, "custom episode: cannot write '%s'\n", path);
		return false;
	}

	bool ok = true;
	if (len > 0)
	{
		if (fseek(f, (long)off, SEEK_SET) != 0)
			ok = false;
		char buf[65536];
		Uint32 left = len;
		while (ok && left > 0)
		{
			const size_t chunk = left < sizeof(buf) ? left : sizeof(buf);
			if (fread(buf, 1, chunk, f) != chunk || fwrite(buf, 1, chunk, out) != chunk)
			{
				ok = false;
				break;
			}
			left -= (Uint32)chunk;
		}
	}
	ok = (fclose(out) == 0) && ok;
	if (!ok)
		fprintf(stderr, "custom episode: failed extracting '%s'\n", path);
	return ok;
}

bool JE_initEpisodeCustom(int index)
{
	if (index < 0 || index >= entryCount)
		return false;
	const CustomEpisodeEntry *e = &entries[index];

	char path[600];
	snprintf(path, sizeof(path), "%s/%s", custom_episode_dir(), e->file);
	FILE *f = fopen(path, "rb");
	if (f == NULL)
	{
		fprintf(stderr, "custom episode: cannot open '%s'\n", path);
		return false;
	}

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	Uint8 hbuf[CLV_HEADER_SIZE];
	ClvHeader h;
	if (size < CLV_HEADER_SIZE || size > CLV_MAX_BYTES ||
	    fread(hbuf, 1, CLV_HEADER_SIZE, f) != CLV_HEADER_SIZE ||
	    !clv_parse_header(hbuf, (Uint32)size, &h) ||
	    !clv_lvl_looks_sane(f, &h))
	{
		fclose(f);
		fprintf(stderr, "custom episode: '%s' is not a loadable container\n", path);
		return false;
	}

	// The engine expects the cube file to exist even when the section is empty.
	const bool ok = extract_section(f, h.lvlOff, h.lvlLen, CUSTOM_EP_LVL_NAME) &&
	                extract_section(f, h.scrOff, h.scrLen, CUSTOM_EP_SCRIPT_NAME) &&
	                extract_section(f, h.cubeOff, h.cubeLen, CUSTOM_EP_CUBES_NAME);
	fclose(f);
	if (!ok)
		return false;

	activeFlag = true;
	SDL_strlcpy(activeFile, e->file, sizeof(activeFile));
	SDL_strlcpy(activeTitle, e->title, sizeof(activeTitle));

	// Force a reload even if the base matches the previous episode.
	activatingNow = true;
	JE_forceEpisodeReload();
	JE_initEpisode(h.base);
	activatingNow = false;
	return true;
}

void customEpisodeDeactivate(void)
{
	if (!activeFlag)
		return;
	activeFlag = false;
	activeFile[0] = '\0';
	activeTitle[0] = '\0';
	// The base number may match the next stock episode.
	JE_forceEpisodeReload();
}

Uint8 *customEpisodeReadWhole(int index, Uint32 *lenOut)
{
	*lenOut = 0;
	if (index < 0 || index >= entryCount)
		return NULL;

	char path[600];
	snprintf(path, sizeof(path), "%s/%s", custom_episode_dir(), entries[index].file);
	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return NULL;

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size < CLV_HEADER_SIZE || size > CLV_MAX_BYTES)
	{
		fclose(f);
		return NULL;
	}

	Uint8 *data = malloc_die((size_t)size);
	const bool ok = fread(data, 1, (size_t)size, f) == (size_t)size;
	fclose(f);
	if (!ok)
	{
		free(data);
		return NULL;
	}
	*lenOut = (Uint32)size;
	return data;
}

bool customEpisodeFileNameValid(const char *name)
{
	if (name == NULL || name[0] == '\0' || name[0] == '.')
		return false;
	const size_t len = strlen(name);
	if (len >= CUSTOM_EPISODE_FILE_LEN || !has_clv_ext(name))
		return false;
	for (size_t i = 0; i < len; ++i)
		if (name[i] == '/' || name[i] == '\\' || name[i] == ':')
			return false;
	return true;
}

bool customEpisodeIdentity(int index, Uint32 *sizeOut, Uint32 *hashOut)
{
	Uint32 len = 0;
	Uint8 *const data = customEpisodeReadWhole(index, &len);
	if (data == NULL)
		return false;

	Uint32 hash = 2166136261u;
	for (Uint32 i = 0; i < len; ++i)
	{
		hash ^= data[i];
		hash *= 16777619u;
	}
	free(data);
	if (hash == 0)
		hash = 1;

	*sizeOut = len;
	*hashOut = hash;
	return true;
}

void qa_test_custom_episode(void)
{
	customEpisodeScan();

	if (entryCount == 0)
	{
		qa_check(!customEpisodeActive() && customEpisodeActiveTitle()[0] == '\0' &&
		         customEpisodeCurrent() == -1,
		         "no custom episodes installed means no custom state anywhere");

		qa_check(customEpisodeCount() == 0 && !customEpisodeAnyPresent(),
		         "with no container present nothing offers a custom-level row");

		const int savedMode = customEndlessMode;
		customEndlessMode = CUSTOM_ENDLESS_ONLY;
		const bool modeStaysOff = customEndlessEffectiveMode() == CUSTOM_ENDLESS_OFF &&
		                          customEpisodeIdCount() == 0;
		customEndlessMode = savedMode;
		qa_check(modeStaysOff,
		         "with no container present Endless cannot be switched onto custom levels");

		struct stat st;
		qa_check(stat(custom_episode_dir(), &st) != 0,
		         "scanning for containers leaves no custom_levels folder behind");

		qa_check(SDL_strcasecmp(JE_episodeDir(), custom_episode_dir()) != 0,
		         "with no container present the engine reads its ordinary episode files");
		return;
	}

	qa_check(JE_initEpisodeCustom(0),
	         "a custom episode container extracts and loads");
	qa_check(customEpisodeActive() && episodeNum == customEpisodeBase(0),
	         "an active custom episode plays under its declared base episode");
	qa_check(lvlNum >= 3 && lvlNum < 43 && lvlNum % 2 == 1,
	         "the custom container's level index is engine-shaped");
	qa_check(weaponPort[1].name[0] != '\0',
	         "the custom container's embedded item tables are loaded");
	qa_check(JE_levelFileCount(customEpisodeBase(0)) == lvlNum / 2u,
	         "JE_levelFileCount answers for the active custom episode");

	Uint32 size = 0, hash = 0;
	qa_check(customEpisodeIdentity(0, &size, &hash) &&
	         size == customEpisodeSize(0) && hash != 0,
	         "a container's transfer identity is readable and sized right");

	{
		char joined[CUSTOM_EPISODE_COLLECTION_LEN];
		const size_t jlen = customEpisodeCollectionString(joined, sizeof(joined));
		char parsedNames[CUSTOM_EPISODE_MAX][CUSTOM_EPISODE_FILE_LEN];
		const int parsed = customEpisodeCollectionNames(joined, parsedNames, CUSTOM_EPISODE_MAX);
		qa_check(jlen > 0 && parsed == customEpisodeCount() &&
		         SDL_strcasecmp(parsedNames[0], customEpisodeFile(0)) == 0 &&
		         !customEpisodeCollectionMissing(joined) &&
		         customEpisodeCollectionMissing("gone_forever.clv"),
		         "a save's collection string round-trips and flags missing containers");
		qa_check(!customEpisodeSaveDepsMissing("", "") &&
		         !customEpisodeSaveDepsMissing(customEpisodeFile(0), joined) &&
		         customEpisodeSaveDepsMissing("gone_forever.clv", "") &&
		         customEpisodeSaveDepsMissing("", "gone_forever.clv"),
		         "a save's dependency lock fires on either missing field and never on stock saves");
	}

	// Restore the first fixture after testing Clear.
	char keptName[CUSTOM_EPISODE_FILE_LEN];
	SDL_strlcpy(keptName, entries[0].file, sizeof(keptName));
	Uint32 keptLen = 0;
	Uint8 *const kept = customEpisodeReadWhole(0, &keptLen);
	if (kept != NULL)
	{
		customEpisodeClearAll();
		struct stat st;
		qa_check(!customEpisodeActive() && customEpisodeCount() == 0 &&
		         !customEpisodeAnyPresent() && stat(custom_episode_dir(), &st) != 0,
		         "Clear .clv leaves no container, no folder, and no custom state");
		qa_check(customEpisodeSaveDownloaded(keptName, kept, keptLen) >= 0 &&
		         customEpisodeCount() >= 1,
		         "a cleared container restores through the download path");
		free(kept);
	}

	// An ordinary episode init must restore stock data.
	JE_initEpisode(1);
	qa_check(!customEpisodeActive() && episodeNum == 1 &&
	         JE_levelFileCount(1) > 0,
	         "an ordinary episode init drops custom mode and reloads stock data");
}

bool customEpisodeContentMatches(const char *fileName, const Uint8 *data, Uint32 len)
{
	if (!customEpisodeFileNameValid(fileName) || data == NULL)
		return false;

	char path[600];
	snprintf(path, sizeof(path), "%s/%s", custom_episode_dir(), fileName);
	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return false;

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	bool same = size >= 0 && (Uint32)size == len;
	if (same)
	{
		fseek(f, 0, SEEK_SET);
		Uint8 chunk[4096];
		Uint32 at = 0;
		while (same && at < len)
		{
			const size_t want = MIN((size_t)(len - at), sizeof(chunk));
			same = fread(chunk, 1, want, f) == want && memcmp(chunk, data + at, want) == 0;
			at += (Uint32)want;
		}
	}
	fclose(f);
	return same;
}

unsigned int customEpisodeWriteCount(void)
{
	return containerWrites;
}

int customEpisodeSaveDownloaded(const char *fileName, const Uint8 *data, Uint32 len)
{
	ClvHeader h;
	if (!customEpisodeFileNameValid(fileName) ||
	    len < CLV_HEADER_SIZE || len > CLV_MAX_BYTES ||
	    !clv_parse_header(data, len, &h))
		return -1;

	// Apply the same level-count check used by the scanner.
	const unsigned int lvlNum = data[h.lvlOff] | (data[h.lvlOff + 1] << 8);
	if (h.lvlLen < 2 || lvlNum < 3 || lvlNum >= 43 || lvlNum % 2 == 0)
		return -1;

	// Preserve an identical file and its timestamp.
	if (customEpisodeContentMatches(fileName, data, len))
	{
		const int existing = customEpisodeFindByFile(fileName);
		if (existing >= 0)
			return existing;
	}

	char path[600];
	snprintf(path, sizeof(path), "%s/%s", custom_episode_dir(), fileName);
	custom_episode_dir_ensure();
	++containerWrites;
	FILE *f = fopen(path, "wb");
	if (f == NULL)
		return -1;
	const bool ok = fwrite(data, 1, len, f) == len;
	if ((fclose(f) != 0) || !ok)
	{
		remove(path);
		return -1;
	}

	customEpisodeScan();
	return customEpisodeFindByFile(fileName);
}
