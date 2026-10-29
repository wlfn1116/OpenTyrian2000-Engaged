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
#ifndef CUSTOM_AUDIO_H
#define CUSTOM_AUDIO_H

#include "opentyr.h"

#include "SDL.h"

#include <stddef.h>

#define CUSTOM_AUDIO_DIR_COUNT 2
#define CUSTOM_AUDIO_PATH_MAX 4096
#define CUSTOM_AUDIO_FILE_MAX_BYTES (128u << 20)
#define CUSTOM_SOUND_SECONDS_MAX 60

typedef enum
{
	CUSTOM_AUDIO_MUSIC = 0,
	CUSTOM_AUDIO_SOUNDS,
	CUSTOM_AUDIO_FOLDER_COUNT
} CustomAudioFolder;

typedef struct CustomTrack CustomTrack;

size_t custom_audio_dirs(CustomAudioFolder folder, const char *dirs[CUSTOM_AUDIO_DIR_COUNT]);
bool custom_audio_find(const char *const *dirs, size_t dirCount, unsigned int number,
                       const char *title, char *path, size_t pathSize);

CustomTrack *custom_track_open(const char *path, int rate, int channels);
void custom_track_close(CustomTrack *track);
void custom_track_rewind(CustomTrack *track);
int custom_track_render(CustomTrack *track, Sint16 *out, int frames, bool loops, bool *looped);

Sint16 *custom_sound_load(const char *path, int rate, int channels, size_t *count);

void custom_audio_qa_root(const char *root);

#endif /* CUSTOM_AUDIO_H */
