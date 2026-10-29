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
#include "sfx.h"

#include "audio.h"
#include "custom_audio.h"
#include "file.h"
#include "opentyr.h"
#include "sndmast.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Sint16 *soundSamples[SOUND_COUNT] = { NULL }; /* [1..soundnum + 9] */  // FKA digiFx
size_t soundSampleCount[SOUND_COUNT] = { 0 }; /* [1..soundnum + 9] */  // FKA fxSize

JE_word tyrMusicVolume, fxVolume;
const JE_word fxPlayVol = 4;
JE_word tempVolume;

static void load_custom_sounds(void)
{
	const char *dirs[CUSTOM_AUDIO_DIR_COUNT];
	const size_t dirCount = custom_audio_dirs(CUSTOM_AUDIO_SOUNDS, dirs);
	char path[CUSTOM_AUDIO_PATH_MAX];
	for (unsigned int i = 0; i < SOUND_COUNT; ++i)
	{
		if (!custom_audio_find(dirs, dirCount, i + 1, soundTitle[i], path, sizeof(path)))
			continue;

		size_t count = 0;
		Sint16 *const samples = custom_sound_load(path, audioSampleRate, AUDIO_OUTPUT_CHANNELS,
		                                          &count);
		if (samples == NULL)
			continue;

		free(soundSamples[i]);
		soundSamples[i] = samples;
		soundSampleCount[i] = count;
	}
}

void loadSndFile(bool xmas)
{
	FILE *f;

	f = dir_fopen_die(data_dir(), "tyrian.snd", "rb");

	Uint16 sfxCount;
	Uint32 sfxPositions[SFX_COUNT + 1];

	// Read number of sounds.
	fread_u16_die(&sfxCount, 1, f);
	if (sfxCount != SFX_COUNT)
		goto die;

	// Read positions of sounds.
	fread_u32_die(sfxPositions, sfxCount, f);

	// Determine end of last sound.
	fseek(f, 0, SEEK_END);
	sfxPositions[sfxCount] = ftell(f);

	// Read samples.
	for (size_t i = 0; i < sfxCount; ++i)
	{
		soundSampleCount[i] = sfxPositions[i + 1] - sfxPositions[i];

		// Sound size cannot exceed 64 KiB.
		if (soundSampleCount[i] > UINT16_MAX)
			goto die;

		free(soundSamples[i]);
		soundSamples[i] = malloc_die(soundSampleCount[i]);

		fseek(f, sfxPositions[i], SEEK_SET);
		fread_u8_die((Uint8 *)soundSamples[i], soundSampleCount[i], f);
	}

	fclose(f);

	f = dir_fopen_die(data_dir(), xmas ? "voicesc.snd" : "voices.snd", "rb");

	Uint16 voiceCount;
	Uint32 voicePositions[VOICE_COUNT + 1];

	// Read number of sounds.
	fread_u16_die(&voiceCount, 1, f);
	if (voiceCount != VOICE_COUNT)
		goto die;

	// Read positions of sounds.
	fread_u32_die(voicePositions, voiceCount, f);

	// Determine end of last sound.
	fseek(f, 0, SEEK_END);
	voicePositions[voiceCount] = ftell(f);

	for (size_t vi = 0; vi < voiceCount; ++vi)
	{
		size_t i = SFX_COUNT + vi;

		soundSampleCount[i] = voicePositions[vi + 1] - voicePositions[vi];

		// Voice sounds have some bad data at the end.
		soundSampleCount[i] = soundSampleCount[i] >= 100
			? soundSampleCount[i] - 100
			: 0;

		// Sound size cannot exceed 64 KiB.
		if (soundSampleCount[i] > UINT16_MAX)
			goto die;

		free(soundSamples[i]);
		soundSamples[i] = malloc_die(soundSampleCount[i]);

		fseek(f, voicePositions[vi], SEEK_SET);
		fread_u8_die((Uint8 *)soundSamples[i], soundSampleCount[i], f);
	}

	fclose(f);

	// Convert samples to output sample format and rate.

	SDL_AudioCVT cvt;
	if (SDL_BuildAudioCVT(&cvt, AUDIO_S8, 1, 11025, AUDIO_S16SYS, AUDIO_OUTPUT_CHANNELS,
	                      audioSampleRate) < 0)
	{
		fprintf(stderr, "error: Failed to build audio converter: %s\n", SDL_GetError());

		for (int i = 0; i < SOUND_COUNT; ++i)
			soundSampleCount[i] = 0;

		return;
	}

	size_t maxSampleSize = 0;
	for (size_t i = 0; i < SOUND_COUNT; ++i)
		maxSampleSize = MAX(maxSampleSize, soundSampleCount[i]);

	cvt.buf = malloc_die(maxSampleSize * cvt.len_mult);

	for (size_t i = 0; i < SOUND_COUNT; ++i)
	{
		cvt.len = soundSampleCount[i];
		memcpy(cvt.buf, soundSamples[i], cvt.len);

		if (SDL_ConvertAudio(&cvt))
		{
			fprintf(stderr, "error: Failed to convert audio: %s\n", SDL_GetError());

			soundSampleCount[i] = 0;

			continue;
		}

		free(soundSamples[i]);
		soundSamples[i] = malloc_die(cvt.len_cvt);

		memcpy(soundSamples[i], cvt.buf, cvt.len_cvt);
		soundSampleCount[i] = cvt.len_cvt / sizeof (Sint16);
	}

	free(cvt.buf);

	if ((custom_sounds & CUSTOM_SOUNDS_SFX) != 0)
		load_custom_sounds();

	return;

die:
	fclose(f);
	fprintf(stderr, "error: Unexpected data was read from a file.\n");
	JE_tyrianExitWithoutSaving(EXIT_FAILURE);
}

void freeSndFile(void)
{
	for (int i = 0; i < SOUND_COUNT; ++i)
	{
		free(soundSamples[i]);
		soundSamples[i] = NULL;
		soundSampleCount[i] = 0;
	}
}

void reloadSndFile(bool xmas)
{
	if (audio_disabled)
		return;

	// loadSndFile frees/reallocs the sample buffers the audio callback reads via
	// the channel pointers, so silence the channels first (see stop_sample_channels).
	stop_sample_channels();
	loadSndFile(xmas);
}

void JE_playSampleNum(JE_byte samplenum)
{
	multiSamplePlay(soundSamples[samplenum-1], soundSampleCount[samplenum-1], 0, fxPlayVol);
}

void JE_changeVolume(JE_word *music, int music_delta, JE_word *sample, int sample_delta)
{
	int music_temp = *music + music_delta,
	    sample_temp = *sample + sample_delta;
	
	if (music_delta)
	{
		if (music_temp > 255)
		{
			music_temp = 255;
			JE_playSampleNum(S_CLINK);
		}
		else if (music_temp < 0)
		{
			music_temp = 0;
			JE_playSampleNum(S_CLINK);
		}
	}
	
	if (sample_delta)
	{
		if (sample_temp > 255)
		{
			sample_temp = 255;
			JE_playSampleNum(S_CLINK);
		}
		else if (sample_temp < 0)
		{
			sample_temp = 0;
			JE_playSampleNum(S_CLINK);
		}
	}
	
	*music = music_temp;
	*sample = sample_temp;
	
	set_volume(*music, *sample);
}
