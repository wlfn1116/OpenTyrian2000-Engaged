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
#include "file.h"

#include "console_platform.h"  // SWITCH_/VITA_ data-directory names
#include "crashlog.h"
#include "opentyr.h"
#include "varz.h"

#include "SDL.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *custom_data_dir = NULL;

#ifdef TARGET_MACOS
/* Read-only data inside the macOS app bundle. Finder launches do not have a useful working
 * directory for the relative entries in data_dir(). */
static const char *macos_bundle_data_dir(void)
{
	static char path[512] = "";

	if (path[0] == '\0')
	{
		char *base = SDL_GetBasePath();
		snprintf(path, sizeof(path), "%sdata", base != NULL ? base : "./");
		SDL_free(base);
	}

	return path;
}
#endif

// finds the Tyrian data directory
const char *data_dir(void)
{
	const char *const dirs[] =
	{
#ifdef __SWITCH__
		// Prefer an SD-card copy (user-updatable) then the read-only data bundled
		// in the .nro. dir_fopen() joins with '/', so "romfs:" becomes "romfs:/file".
		SWITCH_USER_DIR,
		SWITCH_ROMFS_DIR,
#endif
#ifdef __vita__
		// Prefer a memory-card copy (user-updatable) then the read-only data bundled
		// in the VPK at app0:data. dir_fopen() joins with '/'.
		VITA_USER_DIR,
		VITA_DATA_DIR,
#endif
#if defined(__ANDROID__) || defined(TARGET_IOS)
		// The app bundle on iOS, and the copy unpacked out of the APK on Android;
		// mobile_platform_init() resolves both before this runs.
		mobile_data_dir(),
#endif
		custom_data_dir,
#ifdef TARGET_MACOS
		macos_bundle_data_dir(),
#endif
		TYRIAN_DIR,
		"data",
		".",
	};

	static const char *dir = NULL;

	if (dir != NULL)
		return dir;

	for (uint i = 0; i < COUNTOF(dirs); ++i)
	{
		if (dirs[i] == NULL)
			continue;

		FILE *f = dir_fopen(dirs[i], "tyrian1.lvl", "rb");
		if (f)
		{
			fclose(f);

			dir = dirs[i];
			break;
		}
	}

	if (dir == NULL) // data not found
		dir = "";

	return dir;
}

// prepend directory and fopen
FILE *dir_fopen(const char *dir, const char *file, const char *mode)
{
	char *path = malloc_die(strlen(dir) + 1 + strlen(file) + 1);
	sprintf(path, "%s/%s", dir, file);

	FILE *f = fopen(path, mode);

	free(path);

	return f;
}

// warn when dir_fopen fails
FILE *dir_fopen_warn(const char *dir, const char *file, const char *mode)
{
	FILE *f = dir_fopen(dir, file, mode);

	if (f == NULL)
		fprintf(stderr, "warning: failed to open '%s': %s\n", file, strerror(errno));

	return f;
}

// die when dir_fopen fails
FILE *dir_fopen_die(const char *dir, const char *file, const char *mode)
{
	FILE *f = dir_fopen(dir, file, mode);

	if (f == NULL)
	{
		char detail[400];
		snprintf(detail, sizeof(detail), "failed to open required data file '%s': %s",
		         file ? file : "(null)", strerror(errno));
		fprintf(stderr, "error: failed to open '%s': %s\n", file, strerror(errno));
		fprintf(stderr, "error: One or more of the required Tyrian " TYRIAN_VERSION " data files could not be found.\n"
		                "       Please read the README file.\n");
		crashlog_report_fatal("FATAL (missing data file -- dir_fopen_die)", detail);
		JE_tyrianHalt(1);
	}

	return f;
}

// check if file can be opened for reading
bool dir_file_exists(const char *dir, const char *file)
{
	FILE *f = dir_fopen(dir, file, "rb");
	if (f != NULL)
		fclose(f);
	return (f != NULL);
}

// Removing an absent file succeeds.
bool dir_remove_file(const char *dir, const char *file)
{
	char *path = malloc_die(strlen(dir) + 1 + strlen(file) + 1);
	sprintf(path, "%s/%s", dir, file);
	const bool removed = remove(path) == 0 || errno == ENOENT;
	free(path);
	return removed;
}

// returns end-of-file position
long ftell_eof(FILE *f)
{
	long pos = ftell(f);

	fseek(f, 0, SEEK_END);
	long size = ftell(f);

	fseek(f, pos, SEEK_SET);

	return size;
}

OT_RET_NOTNULL void *malloc_die(size_t size)
{
	// malloc(0) may return NULL without failing, so a zero-byte request can't be read as an error
	// (a joystick with no buttons makes one). Round it to a byte the caller won't read, which keeps
	// "NULL means out of memory" true here.
	void *p = malloc(size ? size : 1);
	if (p == NULL)
	{
		char detail[80];
		snprintf(detail, sizeof(detail), "malloc failed: wanted %zu bytes", size);
		fprintf(stderr, "error: Out of memory.\n");
		crashlog_report_fatal("FATAL (allocation failed -- malloc_die)", detail);
		SDL_Quit();
		exit(EXIT_FAILURE);
	}
	return p;
}

void fread_die(void *buffer, size_t size, size_t count, FILE *stream)
{
	size_t result = fread(buffer, size, count, stream);
	if (result != count)
	{
		char detail[160];
		snprintf(detail, sizeof(detail),
		         "fread short read: wanted %zu x %zu = %zu bytes, got %zu (EOF/truncated file)",
		         count, size, size * count, result);
		fprintf(stderr, "error: An unexpected problem occurred while reading from a file.\n");
		crashlog_report_fatal("FATAL (file read failed -- fread_die)", detail);
		SDL_Quit();
		exit(EXIT_FAILURE);
	}
}

void fwrite_die(const void *buffer, size_t size, size_t count, FILE *stream)
{
	size_t result = fwrite(buffer, size, count, stream);
	if (result != count)
	{
		char detail[160];
		snprintf(detail, sizeof(detail),
		         "fwrite short write: wanted %zu x %zu = %zu bytes, wrote %zu (disk full?)",
		         count, size, size * count, result);
		fprintf(stderr, "error: An unexpected problem occurred while writing to a file.\n");
		crashlog_report_fatal("FATAL (file write failed -- fwrite_die)", detail);
		SDL_Quit();
		exit(EXIT_FAILURE);
	}
}
