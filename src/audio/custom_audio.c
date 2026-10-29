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
#include "custom_audio.h"

#include "custom_audio_codecs.h"
#include "file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DECODE_FRAMES 1024
#define DECODE_CHANNELS_MAX 2
#define SOUND_CHUNK_FRAMES 4096
#define FILE_NAME_MAX 256

typedef enum
{
	CODEC_NONE = 0,
	CODEC_WAV,
	CODEC_VORBIS,
	CODEC_MP3,
} Codec;

struct CustomTrack
{
	Uint8 *data;
	Codec codec;
	stb_vorbis *vorbis;
	drmp3 mp3;
	Uint8 *wav;
	Uint32 wavBytes;
	Uint32 wavPos;
	int sourceChannels;
	int sourceFrameBytes;
	int channels;
	SDL_AudioStream *stream;
	bool fresh;
	bool ended;
};

static const char *const folderNames[CUSTOM_AUDIO_FOLDER_COUNT] = { "mus", "sfx" };
static const char *const extensions[] = { "mp3", "ogg", "wav" };

typedef struct
{
	unsigned int number;
	char padded[12];
	char title[FILE_NAME_MAX];
	int rank;
	char name[FILE_NAME_MAX];
} NameMatch;

static char qaRoot[CUSTOM_AUDIO_PATH_MAX];

void custom_audio_qa_root(const char *root)
{
	SDL_strlcpy(qaRoot, root != NULL ? root : "", sizeof(qaRoot));
}

size_t custom_audio_dirs(CustomAudioFolder folder, const char *dirs[CUSTOM_AUDIO_DIR_COUNT])
{
	static char paths[CUSTOM_AUDIO_FOLDER_COUNT][CUSTOM_AUDIO_DIR_COUNT][CUSTOM_AUDIO_PATH_MAX];
	static size_t counts[CUSTOM_AUDIO_FOLDER_COUNT];
	static bool resolved = false;
	static char qaPaths[CUSTOM_AUDIO_FOLDER_COUNT][CUSTOM_AUDIO_PATH_MAX];

	if (qaRoot[0] != '\0')
	{
		snprintf(qaPaths[folder], sizeof(qaPaths[folder]), "%s/%s", qaRoot, folderNames[folder]);
		dirs[0] = qaPaths[folder];
		return 1;
	}

	if (!resolved)
	{
		resolved = true;

		char *const base = SDL_GetBasePath();
		for (int f = 0; f < CUSTOM_AUDIO_FOLDER_COUNT; ++f)
		{
			if (base != NULL)
			{
				snprintf(paths[f][counts[f]++], CUSTOM_AUDIO_PATH_MAX, "%s%s",
				         base, folderNames[f]);
			}
			snprintf(paths[f][counts[f]++], CUSTOM_AUDIO_PATH_MAX, "%s/%s",
			         get_user_directory(), folderNames[f]);
		}
		SDL_free(base);
	}

	for (size_t i = 0; i < counts[folder]; ++i)
		dirs[i] = paths[folder][i];
	return counts[folder];
}

static bool normalize_name(const char *text, size_t length, char *out, size_t outSize)
{
	size_t used = 0;
	for (size_t i = 0; i < length; ++i)
	{
		const char c = text[i];
		const bool upper = c >= 'A' && c <= 'Z';
		if (!upper && !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9'))
			continue;
		if (used + 1 >= outSize)
			return false;
		out[used++] = upper ? (char)(c - 'A' + 'a') : c;
	}
	out[used] = '\0';
	return used > 0;
}

static int stem_kind(const char *stem, size_t length, const NameMatch *match)
{
	if (length == strlen(match->padded) && strncmp(stem, match->padded, length) == 0)
		return 0;

	bool digits = length > 0 && length < 10;
	for (size_t i = 0; digits && i < length; ++i)
		digits = stem[i] >= '0' && stem[i] <= '9';
	if (digits)
		return strtoul(stem, NULL, 10) == match->number ? 1 : -1;

	char normalized[FILE_NAME_MAX];
	if (match->title[0] != '\0' && normalize_name(stem, length, normalized, sizeof(normalized))
	    && strcmp(normalized, match->title) == 0)
		return 2;
	return -1;
}

static bool consider_file(const char *name, void *context)
{
	NameMatch *const match = context;
	const char *const dot = strrchr(name, '.');
	if (dot == NULL)
		return true;

	for (size_t e = 0; e < COUNTOF(extensions); ++e)
	{
		if (SDL_strcasecmp(dot + 1, extensions[e]) != 0)
			continue;

		const int kind = stem_kind(name, (size_t)(dot - name), match);
		if (kind < 0)
			break;

		const int rank = kind * (int)COUNTOF(extensions) + (int)e;
		const bool better = match->rank < 0 || rank < match->rank
		                 || (rank == match->rank && strcmp(name, match->name) < 0);
		if (better)
		{
			match->rank = rank;
			SDL_strlcpy(match->name, name, sizeof(match->name));
		}
		break;
	}
	return true;
}

bool custom_audio_find(const char *const *dirs, size_t dirCount, unsigned int number,
                       const char *title, char *path, size_t pathSize)
{
	NameMatch match;
	match.number = number;
	snprintf(match.padded, sizeof(match.padded), "%02u", number);
	if (title == NULL || !normalize_name(title, strlen(title), match.title, sizeof(match.title)))
		match.title[0] = '\0';

	for (size_t d = 0; d < dirCount; ++d)
	{
		match.rank = -1;
		match.name[0] = '\0';
		dir_each_file(dirs[d], consider_file, &match);
		if (match.rank >= 0)
		{
			snprintf(path, pathSize, "%s/%s", dirs[d], match.name);
			return true;
		}
	}

	path[0] = '\0';
	return false;
}

static Uint8 *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return NULL;

	const long length = ftell_eof(f);
	const bool fits = length > 0 && (unsigned long)length <= CUSTOM_AUDIO_FILE_MAX_BYTES;
	Uint8 *data = fits ? malloc((size_t)length) : NULL;
	if (data != NULL && fread(data, 1, (size_t)length, f) != (size_t)length)
	{
		free(data);
		data = NULL;
	}
	fclose(f);

	*size = data != NULL ? (size_t)length : 0;
	return data;
}

static bool has_magic(const Uint8 *data, size_t size, size_t offset, const char *magic)
{
	const size_t length = strlen(magic);
	return size >= offset + length && memcmp(data + offset, magic, length) == 0;
}

static int open_decoder(CustomTrack *track, size_t size, SDL_AudioFormat *format)
{
	if (has_magic(track->data, size, 0, "OggS"))
	{
		track->vorbis = stb_vorbis_open_memory(track->data, (int)size, NULL, NULL);
		if (track->vorbis == NULL)
			return 0;

		const stb_vorbis_info info = stb_vorbis_get_info(track->vorbis);
		track->codec = CODEC_VORBIS;
		track->sourceChannels = MIN(info.channels, DECODE_CHANNELS_MAX);
		track->sourceFrameBytes = track->sourceChannels * (int)sizeof(Sint16);
		return (int)info.sample_rate;
	}

	if (has_magic(track->data, size, 0, "RIFF") && has_magic(track->data, size, 8, "WAVE"))
	{
		SDL_AudioSpec spec;
		SDL_RWops *const rw = SDL_RWFromConstMem(track->data, (int)size);
		if (SDL_LoadWAV_RW(rw, 1, &spec, &track->wav, &track->wavBytes) == NULL)
			return 0;

		track->codec = CODEC_WAV;
		track->sourceChannels = spec.channels;
		track->sourceFrameBytes = SDL_AUDIO_BITSIZE(spec.format) / 8 * spec.channels;
		if (track->sourceFrameBytes <= 0)
			return 0;
		track->wavBytes -= track->wavBytes % (Uint32)track->sourceFrameBytes;
		*format = spec.format;
		return spec.freq;
	}

	if (!drmp3_init_memory(&track->mp3, track->data, size, NULL))
		return 0;

	track->codec = CODEC_MP3;
	track->sourceChannels = (int)track->mp3.channels;
	track->sourceFrameBytes = track->sourceChannels * (int)sizeof(Sint16);
	return (int)track->mp3.sampleRate;
}

CustomTrack *custom_track_open(const char *path, int rate, int channels)
{
	size_t size = 0;
	Uint8 *const data = read_file(path, &size);
	CustomTrack *const track = data != NULL ? calloc(1, sizeof(*track)) : NULL;
	if (track == NULL)
	{
		free(data);
		fprintf(stderr, "warning: failed to read '%s'\n", path);
		return NULL;
	}
	track->data = data;
	track->channels = channels;

	SDL_AudioFormat format = AUDIO_S16SYS;
	const int sourceRate = open_decoder(track, size, &format);
	if (track->codec == CODEC_WAV)
	{
		free(track->data);
		track->data = NULL;
	}

	if (sourceRate > 0 && track->sourceChannels > 0)
	{
		track->stream = SDL_NewAudioStream(format, (Uint8)track->sourceChannels, sourceRate,
		                                   AUDIO_S16SYS, (Uint8)channels, rate);
	}
	if (track->stream == NULL)
	{
		fprintf(stderr, "warning: failed to decode '%s'\n", path);
		custom_track_close(track);
		return NULL;
	}

	track->fresh = true;
	return track;
}

void custom_track_close(CustomTrack *track)
{
	if (track == NULL)
		return;

	if (track->stream != NULL)
		SDL_FreeAudioStream(track->stream);

	switch (track->codec)
	{
	case CODEC_WAV:
		SDL_FreeWAV(track->wav);
		break;
	case CODEC_VORBIS:
		stb_vorbis_close(track->vorbis);
		break;
	case CODEC_MP3:
		drmp3_uninit(&track->mp3);
		break;
	case CODEC_NONE:
		break;
	}

	free(track->data);
	free(track);
}

static void seek_start(CustomTrack *track)
{
	switch (track->codec)
	{
	case CODEC_WAV:
		track->wavPos = 0;
		break;
	case CODEC_VORBIS:
		stb_vorbis_seek_start(track->vorbis);
		break;
	case CODEC_MP3:
		drmp3_seek_to_pcm_frame(&track->mp3, 0);
		break;
	case CODEC_NONE:
		break;
	}
	track->fresh = true;
}

void custom_track_rewind(CustomTrack *track)
{
	seek_start(track);
	SDL_AudioStreamClear(track->stream);
	track->ended = false;
}

static int feed(CustomTrack *track)
{
	if (track->codec == CODEC_WAV)
	{
		const Uint32 chunk = (Uint32)(DECODE_FRAMES * track->sourceFrameBytes);
		const Uint32 bytes = MIN(chunk, track->wavBytes - track->wavPos);
		if (bytes == 0)
			return 0;
		if (SDL_AudioStreamPut(track->stream, track->wav + track->wavPos, (int)bytes) != 0)
			return -1;
		track->wavPos += bytes;
		return (int)bytes;
	}

	Sint16 pcm[DECODE_FRAMES * DECODE_CHANNELS_MAX];
	int frames;
	if (track->codec == CODEC_VORBIS)
	{
		const int shorts = DECODE_FRAMES * track->sourceChannels;
		frames = stb_vorbis_get_samples_short_interleaved(track->vorbis, track->sourceChannels,
		                                                  pcm, shorts);
	}
	else
	{
		frames = (int)drmp3_read_pcm_frames_s16(&track->mp3, DECODE_FRAMES, pcm);
	}
	if (frames <= 0)
		return 0;

	const int bytes = frames * track->sourceFrameBytes;
	return SDL_AudioStreamPut(track->stream, pcm, bytes) == 0 ? bytes : -1;
}

int custom_track_render(CustomTrack *track, Sint16 *out, int frames, bool loops, bool *looped)
{
	const int frameBytes = track->channels * (int)sizeof(Sint16);
	const int wanted = frames * frameBytes;

	while (!track->ended && SDL_AudioStreamAvailable(track->stream) < wanted)
	{
		const int fed = feed(track);
		if (fed > 0)
		{
			track->fresh = false;
			continue;
		}
		if (fed == 0 && loops && !track->fresh)
		{
			seek_start(track);
			*looped = true;
			continue;
		}

		SDL_AudioStreamFlush(track->stream);
		track->ended = true;
	}

	const int got = SDL_AudioStreamGet(track->stream, out, wanted);
	return got > 0 ? got / frameBytes : 0;
}

Sint16 *custom_sound_load(const char *path, int rate, int channels, size_t *count)
{
	*count = 0;

	CustomTrack *const track = custom_track_open(path, rate, channels);
	if (track == NULL)
		return NULL;

	const size_t limit = (size_t)rate * CUSTOM_SOUND_SECONDS_MAX;
	Sint16 *samples = NULL;
	size_t capacity = 0;
	size_t frames = 0;
	bool failed = false;
	bool looped = false;
	while (frames < limit)
	{
		if (capacity - frames < SOUND_CHUNK_FRAMES)
		{
			const size_t grown = capacity + MAX(capacity, (size_t)SOUND_CHUNK_FRAMES);
			Sint16 *const larger = realloc(samples, grown * (size_t)channels * sizeof(*samples));
			if (larger == NULL)
			{
				failed = true;
				break;
			}
			samples = larger;
			capacity = grown;
		}

		const int want = (int)MIN((size_t)SOUND_CHUNK_FRAMES, limit - frames);
		Sint16 *const out = &samples[frames * (size_t)channels];
		const int got = custom_track_render(track, out, want, false, &looped);
		frames += (size_t)got;
		if (got < want)
			break;
	}
	custom_track_close(track);

	if (failed || frames == 0)
	{
		free(samples);
		return NULL;
	}
	if (frames == limit)
		fprintf(stderr, "warning: '%s' plays only its first %d seconds\n", path,
		        CUSTOM_SOUND_SECONDS_MAX);

	*count = frames * (size_t)channels;
	Sint16 *const fitted = realloc(samples, *count * sizeof(*samples));
	return fitted != NULL ? fitted : samples;
}
