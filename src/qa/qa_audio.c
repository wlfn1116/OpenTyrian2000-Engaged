#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "audio.h"
#include "be_bytes.h"
#include "custom_audio.h"
#include "file.h"
#include "lds_play.h"
#include "musmast.h"
#include "sfx.h"
#include "xmas.h"

#include "SDL.h"
#ifdef WITH_MIDI
#include <midiproc.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Setup > Sound filter presets at the rates audio devices grant. */
void qa_test_sfx_filter(void)
{
	const int rates[] = { 22050, 44100, 48000 };
	for (size_t r = 0; r < COUNTOF(rates); ++r)
	{
		qa_check(sfx_filter_gain(SFX_FILTER_OFF, rates[r], true) == 1.0f,
		         "the Off sound filter leaves the signal alone");
		for (int f = SFX_FILTER_LIGHT; f < SFX_FILTER_COUNT; ++f)
		{
			const float dc = sfx_filter_gain(f, rates[r], false);
			qa_check(dc > 0.99f && dc < 1.01f,
			         "a sound filter keeps a steady level at full volume");
			qa_check(sfx_filter_gain(f, rates[r], true) < 0.01f,
			         "a sound filter silences the highest frequency the device can play");
		}
	}
	qa_check(sfx_filter_gain(SFX_FILTER_LIGHT, 0, false) == 1.0f,
	         "a sound filter without an output rate stays off");
}

#define QA_AUDIO_RATE   44100
#define QA_AUDIO_FRAMES 4096
#define QA_MONO_SOUND_FRAMES 500
#define QA_MONO_SOUND_RATE 22050
#define QA_NAMED_SOUND_FRAMES 180

typedef struct
{
	unsigned int number;
	int frames;
	int step;
	int offset;
} QaWav;

static const QaWav qaLoopWav = { 2, 1500, 11, 0 };
static const QaWav qaOnceWav = { 10, 2000, 7, 100 };
static const QaWav qaSoundWav = { 1, 1000, 13, 50 };

static const unsigned int qaSongsPlayOnce[] = { 9, 10, 18, 24, 30, 33 };
static const unsigned int qaTempoSong = 29;
static const unsigned int qaSongWithoutFile = 3;
static const int qaTempoLoopUpdates = 18;

static Sint16 qaAudioOut[QA_AUDIO_FRAMES * AUDIO_OUTPUT_CHANNELS];

static Sint16 qa_wav_sample(const QaWav *wav, int frame, int channel)
{
	const int left = (frame * wav->step) % 16384 - 8192 + wav->offset;
	return (Sint16)(channel == 0 ? left : -left);
}

static bool qa_wav_matches(const Sint16 *out, int frames, int from, const QaWav *wav)
{
	for (int i = 0; i < frames; ++i)
	{
		for (int c = 0; c < AUDIO_OUTPUT_CHANNELS; ++c)
		{
			const Sint16 want = qa_wav_sample(wav, (from + i) % wav->frames, c);
			if (out[i * AUDIO_OUTPUT_CHANNELS + c] != want)
				return false;
		}
	}
	return true;
}

static bool qa_audio_silent(const Sint16 *out, int frames)
{
	for (int i = 0; i < frames * AUDIO_OUTPUT_CHANNELS; ++i)
	{
		if (out[i] != 0)
			return false;
	}
	return true;
}

static bool qa_audio_centered(const Sint16 *out, int frames)
{
	for (int i = 0; i < frames; ++i)
	{
		if (out[i * AUDIO_OUTPUT_CHANNELS] != out[i * AUDIO_OUTPUT_CHANNELS + 1])
			return false;
	}
	return true;
}

static long qa_audio_energy(const Sint16 *out, int frames, int channel)
{
	long sum = 0;
	for (int i = 0; i < frames; ++i)
		sum += labs((long)out[i * AUDIO_OUTPUT_CHANNELS + channel]);
	return sum;
}

static bool qa_sound_centered(unsigned int sound)
{
	const int frames = (int)(soundSampleCount[sound] / AUDIO_OUTPUT_CHANNELS);
	return qa_audio_centered(soundSamples[sound], frames);
}

static bool qa_path_ends(const char *path, const char *tail)
{
	const size_t pathLength = strlen(path), tailLength = strlen(tail);
	return pathLength >= tailLength && SDL_strcasecmp(path + pathLength - tailLength, tail) == 0;
}

static void qa_custom_audio_root(void)
{
	static char root[600];
	snprintf(root, sizeof(root), "%s/custom_audio", qa_fixture_dir);
	custom_audio_qa_root(root);
}

static CustomTrack *qa_open_custom_music(unsigned int number)
{
	const char *dirs[CUSTOM_AUDIO_DIR_COUNT];
	char path[CUSTOM_AUDIO_PATH_MAX];
	const size_t count = custom_audio_dirs(CUSTOM_AUDIO_MUSIC, dirs);
	if (!custom_audio_find(dirs, count, number, NULL, path, sizeof(path)))
		return NULL;
	return custom_track_open(path, QA_AUDIO_RATE, AUDIO_OUTPUT_CHANNELS);
}

static int qa_track_frames(CustomTrack *track, bool loops, int limit, bool *looped)
{
	static Sint16 scratch[QA_AUDIO_FRAMES * AUDIO_OUTPUT_CHANNELS];
	int total = 0;
	for (;;)
	{
		Sint16 *const out = total == 0 ? qaAudioOut : scratch;
		const int got = custom_track_render(track, out, QA_AUDIO_FRAMES, loops, looped);
		total += got;
		if (got < QA_AUDIO_FRAMES || total >= limit)
			return total;
	}
}

static bool qa_track_restarts_exactly(CustomTrack *track)
{
	static Sint16 first[QA_AUDIO_FRAMES * AUDIO_OUTPUT_CHANNELS];
	bool looped = false;
	memcpy(first, qaAudioOut, sizeof(first));
	custom_track_rewind(track);
	qa_track_frames(track, false, QA_AUDIO_FRAMES, &looped);
	return memcmp(first, qaAudioOut, sizeof(first)) == 0;
}

static void qa_test_custom_audio_lookup(void)
{
	const char *dirs[CUSTOM_AUDIO_DIR_COUNT];
	char path[CUSTOM_AUDIO_PATH_MAX];

	size_t count = custom_audio_dirs(CUSTOM_AUDIO_MUSIC, dirs);
	qa_check(count >= 1 && qa_path_ends(dirs[count - 1], "/mus"),
	         "custom music is looked up in a mus folder");
	count = custom_audio_dirs(CUSTOM_AUDIO_SOUNDS, dirs);
	qa_check(count >= 1 && qa_path_ends(dirs[count - 1], "/sfx"),
	         "custom sounds are looked up in an sfx folder");
#ifdef WITH_MIDI
	bool searched = true;
	for (int folder = 0; folder < CUSTOM_AUDIO_FOLDER_COUNT; ++folder)
	{
		count = custom_audio_dirs((CustomAudioFolder)folder, dirs);
		searched = searched && count >= 1;
		for (size_t i = 0; i < count; ++i)
			searched = searched && audio_qa_soundfont_searches(dirs[i]);
	}
	qa_check(searched, "SoundFonts are looked for in the mus and sfx folders");
#endif

	qa_custom_audio_root();
	count = custom_audio_dirs(CUSTOM_AUDIO_MUSIC, dirs);
	qa_check(count == 1 && custom_audio_find(dirs, count, 1, NULL, path, sizeof(path))
	         && qa_path_ends(path, "/1.ogg"), "a file named without a leading zero is found");
	qa_check(custom_audio_find(dirs, count, 3, NULL, path, sizeof(path))
	         && qa_path_ends(path, "/03.mp3"), "a file named with a leading zero is found");
	qa_check(custom_audio_find(dirs, count, 5, NULL, path, sizeof(path))
	         && qa_path_ends(path, "/05.wav"), "the zero-padded name wins over the short one");
	qa_check(custom_audio_find(dirs, count, 10, NULL, path, sizeof(path))
	         && qa_path_ends(path, "/10.wav"), "two-digit numbers are found");
	qa_check(!custom_audio_find(dirs, count, 4, musicTitle[3], path, sizeof(path))
	         && path[0] == '\0', "a song without a file under its number or name finds nothing");
	qa_check(custom_audio_find(dirs, count, 41, musicTitle[40], path, sizeof(path))
	         && qa_path_ends(path, "/beer.wav"), "a song's name finds its file in any case");
	qa_check(custom_audio_find(dirs, count, 30, musicTitle[29], path, sizeof(path))
	         && qa_path_ends(path, "/tyrian - the song.wav"),
	         "a song's name matches without its punctuation");
	qa_check(custom_audio_find(dirs, count, 2, musicTitle[1], path, sizeof(path))
	         && qa_path_ends(path, "/02.wav"), "a number wins over a name");
	count = custom_audio_dirs(CUSTOM_AUDIO_SOUNDS, dirs);
	qa_check(custom_audio_find(dirs, count, 7, NULL, path, sizeof(path))
	         && qa_path_ends(path, "/07.wav"), "an upper-case extension is found");
	qa_check(custom_audio_find(dirs, count, 4, soundTitle[3], path, sizeof(path))
	         && qa_path_ends(path, "/Explsm.wav"), "a sound's name finds its file");
	qa_check(custom_audio_find(dirs, count, 8, soundTitle[7], path, sizeof(path))
	         && qa_path_ends(path, "/0008.wav"), "a number with extra leading zeros is found");
	custom_audio_qa_root(NULL);
}

static void qa_test_custom_audio_decoders(void)
{
	qa_custom_audio_root();
	bool looped = false;

	CustomTrack *track = qa_open_custom_music(1);
	qa_check(track != NULL, "an Ogg Vorbis file opens");
	if (track != NULL)
	{
		const int frames = qa_track_frames(track, false, QA_AUDIO_RATE, &looped);
		qa_check(abs(frames - QA_AUDIO_RATE / 2) <= QA_AUDIO_RATE / 40,
		         "an Ogg Vorbis file plays for its length");
		const long left = qa_audio_energy(qaAudioOut, QA_AUDIO_FRAMES, 0);
		const long right = qa_audio_energy(qaAudioOut, QA_AUDIO_FRAMES, 1);
		qa_check(left > 1000L * QA_AUDIO_FRAMES && right * 100 < left,
		         "an Ogg Vorbis file keeps its left and right channels apart");
		qa_check(qa_track_restarts_exactly(track), "a rewound Ogg Vorbis file starts over exactly");

		custom_track_rewind(track);
		looped = false;
		const int longer = 3 * QA_AUDIO_RATE / 2;
		qa_check(qa_track_frames(track, true, longer, &looped) >= longer && looped,
		         "a looping Ogg Vorbis file plays on past its end");
		custom_track_close(track);
	}

	track = qa_open_custom_music(3);
	qa_check(track != NULL, "an MP3 file opens");
	if (track != NULL)
	{
		const int frames = qa_track_frames(track, false, QA_AUDIO_RATE, &looped);
		qa_check(abs(frames - QA_AUDIO_RATE / 2) <= QA_AUDIO_RATE / 40,
		         "a 22 kHz MP3 file plays for its length at the device rate");
		qa_check(qa_audio_centered(qaAudioOut, QA_AUDIO_FRAMES)
		         && qa_audio_energy(qaAudioOut, QA_AUDIO_FRAMES, 0) > 1000L * QA_AUDIO_FRAMES,
		         "a mono MP3 file plays the same on both channels");
		qa_check(qa_track_restarts_exactly(track), "a rewound MP3 file starts over exactly");
		custom_track_close(track);
	}

	track = qa_open_custom_music(qaLoopWav.number);
	qa_check(track != NULL, "a WAV file opens");
	if (track != NULL)
	{
		looped = false;
		const int frames = qa_track_frames(track, false, QA_AUDIO_RATE, &looped);
		qa_check(frames == qaLoopWav.frames && !looped
		         && qa_wav_matches(qaAudioOut, qaLoopWav.frames, 0, &qaLoopWav),
		         "a WAV file plays every sample once");
		custom_track_rewind(track);
		qa_check(custom_track_render(track, qaAudioOut, 2000, true, &looped) == 2000 && looped
		         && qa_wav_matches(qaAudioOut, 2000, 0, &qaLoopWav),
		         "a looping track continues from its first sample");
		custom_track_close(track);
	}

	char path[CUSTOM_AUDIO_PATH_MAX];
	size_t samples = 0;
	snprintf(path, sizeof(path), "%s/custom_audio/long_100hz.wav", qa_fixture_dir);
	Sint16 *sound = custom_sound_load(path, 100, 1, &samples);
	qa_check(sound != NULL && samples == 100u * CUSTOM_SOUND_SECONDS_MAX,
	         "an overlong sound is cut at the sound length limit");
	free(sound);

	snprintf(path, sizeof(path), "%s/upstream_tyrian.sav", qa_fixture_dir);
	sound = custom_sound_load(path, QA_AUDIO_RATE, AUDIO_OUTPUT_CHANNELS, &samples);
	qa_check(sound == NULL && samples == 0, "a file that is not audio loads nothing");
	free(sound);

	snprintf(path, sizeof(path), "%s/custom_audio/missing.wav", qa_fixture_dir);
	qa_check(custom_track_open(path, QA_AUDIO_RATE, AUDIO_OUTPUT_CHANNELS) == NULL,
	         "a missing file opens nothing");

	custom_audio_qa_root(NULL);
}

void qa_test_custom_audio_files(void)
{
	qa_test_custom_audio_lookup();
	qa_test_custom_audio_decoders();
}

static void qa_render_frames(int frames)
{
	audio_qa_render(qaAudioOut, frames);
}

static bool qa_audio_begin(void)
{
	const bool began = audio_qa_begin(QA_AUDIO_RATE);
	qa_check(began, "the mixer runs without an audio device");
	if (began)
		set_volume(255, 255);
	return began;
}

static int qa_lds_updates_to_loop(void)
{
	int updates = 0;
	while (playing && !songlooped && updates < 1000)
	{
		lds_update();
		++updates;
	}
	return updates;
}

static void qa_test_lds_dry_run_tempo(void)
{
	char path[600];
	snprintf(path, sizeof(path), "%s/lds_tempo_change.lds", qa_fixture_dir);
	FILE *const lds = fopen(path, "rb");
	qa_check(lds != NULL, "the tempo-change song fixture opens");
	if (lds == NULL)
		return;

	const unsigned int size = (unsigned int)ftell_eof(lds);
	lds_load(lds, 0, size);
	const int fresh = qa_lds_updates_to_loop();
	lds_load(lds, 0, size);
	const bool once = lds_plays_once();
	const int scanned = qa_lds_updates_to_loop();
	qa_check(!once && fresh == qaTempoLoopUpdates && scanned == fresh,
	         "a dry run restores the tempo a song was loaded with");
	fclose(lds);
}

static void qa_test_song_endings(void)
{
	if (!qa_audio_begin())
		return;

	for (unsigned int song = 0; song < MUSIC_NUM; ++song)
	{
		bool once = false;
		for (size_t i = 0; i < COUNTOF(qaSongsPlayOnce); ++i)
			once = once || qaSongsPlayOnce[i] == song;
		qa_check(audio_qa_song_loops(song) == !once,
		         "a song's dry run finds whether it loops or ends");
#ifdef WITH_MIDI
		qa_check(audio_qa_midi_loops(song) == !once,
		         "the MIDI conversion agrees on whether a song loops");
#endif
	}
	qa_test_lds_dry_run_tempo();
	audio_qa_end();

	static Sint16 plain[QA_AUDIO_FRAMES * AUDIO_OUTPUT_CHANNELS * 8];
	static Sint16 scanned[QA_AUDIO_FRAMES * AUDIO_OUTPUT_CHANNELS * 8];
	const int frames = QA_AUDIO_FRAMES * 8;
	if (!qa_audio_begin())
		return;
	play_song(qaTempoSong);
	audio_qa_render(plain, frames);
	audio_qa_end();
	if (!qa_audio_begin())
		return;
	play_song(qaTempoSong);
	audio_qa_song_loops(qaTempoSong);
	audio_qa_render(scanned, frames);
	audio_qa_end();
	qa_check(memcmp(plain, scanned, sizeof(plain)) == 0,
	         "a song's dry run leaves it to play from the top");
	qa_check(qa_audio_centered(plain, frames) && qa_audio_energy(plain, frames, 0) > 0,
	         "the built-in synth plays the same on both channels");
}

static void qa_test_custom_music_playback(void)
{
	const unsigned int loopSong = qaLoopWav.number - 1;
	const unsigned int onceSong = qaOnceWav.number - 1;

	if (!qa_audio_begin())
		return;

	play_song(loopSong);
	qa_check(playing && !songlooped, "a custom track starts playing");
	qa_render_frames(2100);
	qa_check(qa_wav_matches(qaAudioOut, 2100, 0, &qaLoopWav),
	         "a looping custom track wraps to its start");
	qa_check(playing && songlooped, "a wrapped custom track reports the loop and plays on");

	restart_song();
	qa_check(playing && !songlooped, "restarting a custom track clears the loop");
	qa_render_frames(400);
	qa_check(qa_wav_matches(qaAudioOut, 400, 0, &qaLoopWav),
	         "restarting plays a custom track from its start");

	stop_song();
	qa_render_frames(300);
	qa_check(qa_audio_silent(qaAudioOut, 300), "a stopped custom track is silent");
	play_song(loopSong);
	qa_render_frames(300);
	qa_check(qa_wav_matches(qaAudioOut, 300, 400, &qaLoopWav),
	         "asking for the stopped song again resumes where it stopped");

	set_volume(0, 255);
	qa_render_frames(200);
	qa_check(qa_audio_silent(qaAudioOut, 200), "music volume zero silences a custom track");
	set_volume(255, 255);

	fade_song();
	int faded = 0;
	long firstEnergy = -1, lastEnergy = 0;
	while (playing && faded < 10 * QA_AUDIO_RATE)
	{
		qa_render_frames(1024);
		const long energy = qa_audio_energy(qaAudioOut, 1024, 0);
		if (firstEnergy < 0)
			firstEnergy = energy;
		if (playing)
			lastEnergy = energy;
		faded += 1024;
	}
	qa_check(!playing && !songlooped, "a faded custom track ends like a finished one");
	qa_check(faded > 7 * QA_AUDIO_RATE / 2 && faded <= 39 * QA_AUDIO_RATE / 10,
	         "a custom track fades over the same time as the built-in synth");
	qa_check(firstEnergy > 0 && lastEnergy * 20 < firstEnergy,
	         "a custom track fades towards silence");
	qa_render_frames(500);
	qa_check(qa_audio_silent(qaAudioOut, 500), "a faded custom track stays silent");

	play_song(onceSong);
	qa_render_frames(qaOnceWav.frames + 500);
	qa_check(qa_wav_matches(qaAudioOut, qaOnceWav.frames, 0, &qaOnceWav)
	         && qa_audio_silent(&qaAudioOut[qaOnceWav.frames * AUDIO_OUTPUT_CHANNELS], 500),
	         "a play-once custom track plays once, then falls silent");
	qa_check(!playing && !songlooped, "a finished play-once track reports its end");
	play_song(onceSong);
	qa_render_frames(200);
	qa_check(!playing && qa_audio_silent(qaAudioOut, 200),
	         "asking for a finished jingle again keeps it finished");
	restart_song();
	qa_render_frames(300);
	qa_check(playing && qa_wav_matches(qaAudioOut, 300, 0, &qaOnceWav),
	         "restarting replays a finished jingle");

	play_song(qaSongWithoutFile);
	long fallback = 0;
	bool centered = true;
	for (int chunk = 0; chunk < 16; ++chunk)
	{
		qa_render_frames(1024);
		fallback += qa_audio_energy(qaAudioOut, 1024, 0);
		centered = centered && qa_audio_centered(qaAudioOut, 1024);
	}
	qa_check(playing && centered && fallback > 0,
	         "a song without a file falls back to the built-in synth");

	play_song(loopSong);
	qa_render_frames(100);
	qa_check(qa_wav_matches(qaAudioOut, 100, 0, &qaLoopWav),
	         "a custom track takes over from the built-in synth");
	set_custom_sounds(CUSTOM_SOUNDS_OFF);
	qa_render_frames(QA_AUDIO_FRAMES);
	qa_check(qa_audio_centered(qaAudioOut, QA_AUDIO_FRAMES)
	         && !qa_wav_matches(qaAudioOut, QA_AUDIO_FRAMES, 0, &qaLoopWav),
	         "turning custom music off hands the song to the built-in synth");
	set_custom_sounds(CUSTOM_SOUNDS_OST);
	qa_render_frames(300);
	qa_check(qa_wav_matches(qaAudioOut, 300, 0, &qaLoopWav),
	         "turning custom music on starts its track");

	audio_qa_end();
}

void qa_test_custom_music(void)
{
	const int savedSounds = custom_sounds;

	custom_sounds = CUSTOM_SOUNDS_OFF;
	qa_test_song_endings();

	qa_custom_audio_root();
	custom_sounds = CUSTOM_SOUNDS_OST;
	qa_test_custom_music_playback();

	custom_audio_qa_root(NULL);
	custom_sounds = savedSounds;
}

void qa_test_custom_sounds(void)
{
	const int savedSounds = custom_sounds;
	const int savedFilter = sfx_filter;

	qa_custom_audio_root();
	custom_sounds = CUSTOM_SOUNDS_SFX;
	sfx_filter = SFX_FILTER_OFF;

	if (qa_audio_begin())
	{
		loadSndFile(xmas);

		const unsigned int custom = qaSoundWav.number - 1;
		qa_check(soundSampleCount[custom] == (size_t)qaSoundWav.frames * AUDIO_OUTPUT_CHANNELS
		         && qa_wav_matches(soundSamples[custom], qaSoundWav.frames, 0, &qaSoundWav),
		         "a stereo custom sound replaces the built-in one sample for sample");
		const int converted = (int)(soundSampleCount[1] / AUDIO_OUTPUT_CHANNELS);
		const int expected = QA_MONO_SOUND_FRAMES * QA_AUDIO_RATE / QA_MONO_SOUND_RATE;
		qa_check(abs(converted - expected) <= expected / 50 && qa_sound_centered(1),
		         "a mono 8-bit custom sound plays centered at the device rate");
		qa_check(soundSampleCount[6] > 0 && qa_sound_centered(6),
		         "a custom sound with an upper-case extension is used");
		const int named = (int)(soundSampleCount[3] / AUDIO_OUTPUT_CHANNELS);
		qa_check(abs(named - QA_NAMED_SOUND_FRAMES * QA_AUDIO_RATE / QA_MONO_SOUND_RATE) <= 8,
		         "a custom sound named after its sound replaces it");
		qa_check(soundSampleCount[4] > 0 && soundSampleCount[4] % AUDIO_OUTPUT_CHANNELS == 0
		         && qa_sound_centered(4),
		         "a sound without a file keeps the built-in one, centered");

		multiSamplePlay(soundSamples[custom], soundSampleCount[custom], 0, 7);
		qa_render_frames(qaSoundWav.frames + 200);
		qa_check(qa_wav_matches(qaAudioOut, qaSoundWav.frames, 0, &qaSoundWav)
		         && qa_audio_silent(&qaAudioOut[qaSoundWav.frames * AUDIO_OUTPUT_CHANNELS], 200),
		         "a stereo sound reaches each channel as recorded");

		sfx_filter = SFX_FILTER_LIGHT;
		multiSamplePlay(soundSamples[custom], soundSampleCount[custom], 0, 7);
		qa_render_frames(qaSoundWav.frames);
		bool mirrored = true;
		bool filtered = false;
		for (int i = 0; i < qaSoundWav.frames; ++i)
		{
			const int left = qaAudioOut[i * AUDIO_OUTPUT_CHANNELS];
			const int right = qaAudioOut[i * AUDIO_OUTPUT_CHANNELS + 1];
			mirrored = mirrored && abs(left + right) <= 1;
			filtered = filtered || left != qa_wav_sample(&qaSoundWav, i, 0);
		}
		qa_check(mirrored && filtered, "the sound filter runs on each channel separately");
		sfx_filter = SFX_FILTER_OFF;

		set_custom_sounds(CUSTOM_SOUNDS_OFF);
		qa_check(soundSampleCount[custom] > 0
		         && soundSampleCount[custom] != (size_t)qaSoundWav.frames * AUDIO_OUTPUT_CHANNELS
		         && qa_sound_centered(custom),
		         "turning custom sounds off brings the built-in sounds back");

		freeSndFile();
		audio_qa_end();
	}

	custom_sounds = savedSounds;
	sfx_filter = savedFilter;
	custom_audio_qa_root(NULL);
}

#ifdef WITH_MIDI

/* LDS detune, envelope release, channel ownership, and truncation checks. */

#define QA_LDS_PATCH_BYTES 46
#define QA_LDS_TEMPO       7    // a row every 8 ticks, so one position spans 512 of them
#define QA_LDS_ROWS        64
#define QA_LDS_NOTE        60   // pattern note byte, in whole semitones
#define QA_LDS_VELOCITY    100
#define QA_LDS_WHEEL_RANGE 12   // semitones, as MIDIProcessorLDS.cpp announces over RPN
#define QA_LDS_BEND_CENTER 8192

/* Program identifies voice groups in this probe. Detune uses 1/16 semitone units, and channel 8
 * remains free for the stop command. */
static const struct
{
	JE_byte program;
	JE_byte level;    // pattern volume for this voice, 0 to 63
	JE_byte carMisc;  // carrier register 0x20; bit 5 makes the voice sustain
	JE_byte carAD;    // carrier attack and decay rates
	JE_byte carSR;    // carrier sustain level and release rate
	int finetune;     // as the patch record stores it
	int detune;       // what should survive: this patch's offset within its group
} qaLdsVoices[] = {
	{ 48, 10, 0x00, 0x00, 0x00,   0,   0 },  // three layers of one voice, already centred
	{ 48, 14, 0x00, 0x00, 0x00,  -2,  -2 },
	{ 48, 18, 0x00, 0x00, 0x00,   2,   2 },
	{ 49, 22, 0x00, 0x00, 0x00,  12,  12 },  // spread wide enough that the offset moves the key
	{ 49, 26, 0x00, 0x00, 0x00, -12, -12 },
	{ 50, 30, 0x00, 0x05, 0xF0, -12,   0 },  // standing alone: voicing stays out of the synth.
	{ 51, 34, 0x00, 0x02, 0xF0,  32,   0 },  // 5 decays inside the song, 6 outlasts it,
	{ 52, 38, 0x20, 0x05, 0xF0,   5,   0 },  // 7 sustains. None of the three may overrun.
};

typedef struct
{
	int key;     // -1 until the voice sounds
	int bend;    // wheel value in force when it sounded
	int notes;   // note-ons seen, so a repeat is caught
	int level;   // -1 until a volume controller reaches this channel
	int levels;  // volume controllers seen, so a stray one is caught
	int held;    // ticks between the note-on and its release, -1 while unreleased
	unsigned on; // tick the note sounded on
} QaLdsVoice;

static JE_byte *qa_lds_put16(JE_byte *p, unsigned value)
{
	*p++ = (JE_byte)(value & 0xff);
	*p++ = (JE_byte)((value >> 8) & 0xff);
	return p;
}

/* One position holding one note per voice and then a stop, so every patch sounds exactly once and
 * the converter's walk ends without needing a position jump. Returns the song's length. */
static size_t qa_lds_build(JE_byte *out)
{
	const unsigned voices = COUNTOF(qaLdsVoices);
	JE_byte *p = out;

	*p++ = 0;                // melodic mode
	p = qa_lds_put16(p, 0);  // speed, which the converter ignores
	*p++ = QA_LDS_TEMPO;     // ticks per row, less one
	*p++ = QA_LDS_ROWS;      // rows per position
	for (unsigned i = 0; i < 9; ++i)
		*p++ = 0;            // no channel delay
	*p++ = 0;                // percussion register

	p = qa_lds_put16(p, voices);
	for (unsigned v = 0; v < voices; ++v)
	{
		memset(p, 0, QA_LDS_PATCH_BYTES);
		p[14] = (JE_byte)(qaLdsVoices[v].finetune & 0xff);  // finetune, a signed byte on disk
		p[5] = qaLdsVoices[v].carMisc;
		p[7] = qaLdsVoices[v].carAD;
		p[8] = qaLdsVoices[v].carSR;
		p[40] = qaLdsVoices[v].program;
		p[41] = QA_LDS_VELOCITY;
		p += QA_LDS_PATCH_BYTES;
	}

	p = qa_lds_put16(p, 1);  // one position
	for (unsigned ch = 0; ch < 9; ++ch)
	{
		p = qa_lds_put16(p, ch * 6);  // byte offset into this channel's pattern words
		*p++ = 0;                     // transpose
	}

	p = qa_lds_put16(p, 0);  // digital sample count

	for (unsigned ch = 0; ch < 9; ++ch)
	{
		// Voices set volume, play once, then wait. Channel 8 stops on the final row.
		const bool voiced = ch < voices;

		p = qa_lds_put16(p, voiced ? 0xfd00u | qaLdsVoices[ch].level : 0x8000u | (QA_LDS_ROWS - 2));
		p = qa_lds_put16(p, voiced ? ((unsigned)QA_LDS_NOTE << 8) | ch : 0xfc00u);

		// Omit unread trailing words so truncation checks cover the parser's actual boundary.
		if (voiced)
			p = qa_lds_put16(p, 0x8000u | (QA_LDS_ROWS - 3));
	}

	return (size_t)(p - out);
}

static const JE_byte *qa_smf_vlq(const JE_byte *p, const JE_byte *end, unsigned *out)
{
	unsigned value = 0;

	for (int i = 0; i < 4 && p < end; ++i)
	{
		value = (value << 7) | (*p & 0x7f);
		if ((*p++ & 0x80) == 0)
		{
			*out = value;
			return p;
		}
	}
	return NULL;
}

/* Walk every track, following running status, and record the key and the wheel value in force for
 * each channel's first note-on. */
static bool qa_smf_scan(const JE_byte *data, size_t size, QaLdsVoice *voice, unsigned voices)
{
	const JE_byte *p = data, *end = data + size;
	int bend[16];

	if (size < 14 || memcmp(p, "MThd", 4) != 0)
		return false;
	p += 8 + be_read32(p + 4);

	for (unsigned i = 0; i < COUNTOF(bend); ++i)
		bend[i] = QA_LDS_BEND_CENTER;

	while (end - p >= 8)
	{
		const unsigned length = be_read32(p + 4);
		const JE_byte *track = p + 8;
		const JE_byte *trackEnd = track + length;
		JE_byte status = 0;
		unsigned now = 0;

		if (memcmp(p, "MTrk", 4) != 0 || (size_t)(end - track) < length)
			return false;
		p = trackEnd;

		while (track < trackEnd)
		{
			unsigned delta, payload;

			track = qa_smf_vlq(track, trackEnd, &delta);
			now += delta;
			if (track == NULL || track >= trackEnd)
				return false;

			if (*track & 0x80)
				status = *track++;
			if (status < 0x80)
				return false;

			if (status == 0xff)  // meta: a type byte, then a counted payload
			{
				if (track >= trackEnd)
					return false;
				++track;
				track = qa_smf_vlq(track, trackEnd, &payload);
			}
			else if (status == 0xf0 || status == 0xf7)  // sysex: a counted payload
			{
				track = qa_smf_vlq(track, trackEnd, &payload);
			}
			else
			{
				const unsigned kind = status & 0xf0;
				const unsigned channel = status & 0x0f;

				payload = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
				if ((size_t)(trackEnd - track) < payload)
					return false;

				if (kind == 0xb0 && track[0] == 7 && channel < voices)
				{
					if (voice[channel].levels++ == 0)
						voice[channel].level = track[1];
				}
				else if (kind == 0xe0)
					bend[channel] = (int)track[0] | ((int)track[1] << 7);
				else if (kind == 0x90 && track[1] != 0 && channel < voices)
				{
					if (voice[channel].notes++ == 0)
					{
						voice[channel].key = track[0];
						voice[channel].bend = bend[channel];
						voice[channel].on = now;
					}
				}
				else if (kind == 0x80 && channel < voices && voice[channel].held < 0
				         && voice[channel].notes > 0)
				{
					voice[channel].held = (int) (now - voice[channel].on);
				}
			}

			if (track == NULL || (size_t)(trackEnd - track) < payload)
				return false;
			track += payload;
		}
	}
	return true;
}

void qa_test_lds_midi_detune(void)
{
	const unsigned voices = COUNTOF(qaLdsVoices);
	JE_byte song[640];
	QaLdsVoice voice[16];
	JE_byte *smf = NULL;
	size_t smfSize = 0;

	qa_check(voices <= 8, "the detune probe leaves a channel free to stop the song");

	const size_t songSize = qa_lds_build(song);
	qa_check(songSize <= sizeof(song), "the probe song fits its buffer");

	HMIDIContainer container = MIDPROC_Container_Create();
	qa_check(MIDPROC_Process(song, songSize, "lds", container),
	         "the converter reads the probe song");

	MIDPROC_Container_SerializeAsSMF(container, &smf, &smfSize);
	qa_check(smf != NULL && smfSize > 0, "the converted song serializes to an SMF");

	for (unsigned i = 0; i < COUNTOF(voice); ++i)
	{
		voice[i].key = -1;
		voice[i].bend = QA_LDS_BEND_CENTER;
		voice[i].notes = 0;
		voice[i].level = -1;
		voice[i].levels = 0;
		voice[i].held = -1;
		voice[i].on = 0;
	}
	qa_check(smf != NULL && qa_smf_scan(smf, smfSize, voice, voices), "the SMF parses");

	for (unsigned v = 0; v < voices; ++v)
	{
		const int target = (QA_LDS_NOTE * 16) + qaLdsVoices[v].detune;
		const int wantKey = (target + 8) >> 4;
		const int wantBend = QA_LDS_BEND_CENTER
		                   + (((target - (wantKey * 16)) * 512) / QA_LDS_WHEEL_RANGE);
		const int wantLevel = (qaLdsVoices[v].level & 0x3f) * 127 / 63;

		if (voice[v].notes != 1 || voice[v].key != wantKey || voice[v].bend != wantBend
		    || voice[v].levels != 1 || voice[v].level != wantLevel)
			fprintf(stderr,
			        "# voice %u: %d note(s), key %d bend %d level %d (x%d), want %d / %d / %d\n",
			        v, voice[v].notes, voice[v].key, voice[v].bend, voice[v].level, voice[v].levels,
			        wantKey, wantBend, wantLevel);

		qa_check(voice[v].notes == 1, "a detuned patch sounds exactly once");
		qa_check(voice[v].key == wantKey, "a patch sounds the key its reduced detune lands on");
		qa_check(voice[v].bend == wantBend, "a patch leaves its detune remainder on the wheel");
		qa_check(voice[v].levels == 1, "a voice takes exactly one volume controller");
		qa_check(voice[v].level == wantLevel, "a volume controller reaches its own voice");
	}

	/* Voice 5 decays before the end, voice 6 outlasts it, and voice 7 sustains. */
	qa_check(voices == 8, "the probe still has the eight voices these checks name");
	if (voices == 8)
	{
		const int songEnd = voice[0].held;

		fprintf(stderr, "# held ticks: decays %d, outlasts %d, sustains %d, song end %d\n",
		        voice[5].held, voice[6].held, voice[7].held, songEnd);
		qa_check(voice[5].held > 0, "a decaying voice is released by its envelope");
		qa_check(voice[5].held < songEnd, "a decaying voice goes quiet before the song ends");
		qa_check(voice[6].held == songEnd, "an envelope past the song end releases at the end");
		qa_check(voice[7].held == songEnd, "an envelope never cuts a sustaining voice short");
	}

	MIDPROC_FreeSerialized(smf);
	MIDPROC_Container_Delete(container);

	/* Every proper prefix must be rejected. Sanitizers cover out-of-bounds reads. */
	size_t accepted = 0, firstAccepted = 0;
	for (size_t cut = 1; cut < songSize; ++cut)
	{
		HMIDIContainer truncated = MIDPROC_Container_Create();

		if (MIDPROC_Process(song, cut, "lds", truncated))
		{
			if (accepted++ == 0)
				firstAccepted = cut;
		}

		MIDPROC_Container_Delete(truncated);
	}

	if (accepted != 0)
		fprintf(stderr, "# %u of %u truncations accepted, first at %u byte(s)\n",
		        (unsigned)accepted, (unsigned)songSize - 1, (unsigned)firstAccepted);
	qa_check(accepted == 0, "the converter rejects every truncation of the probe song");
}

#endif /* WITH_MIDI */
