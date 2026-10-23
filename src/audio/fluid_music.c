/* FluidSynth player implementation. See fluid_music.h. */
#include "fluid_music.h"

#ifdef WITH_MIDI

#include "midi_smf.h"

#include "SDL.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 5287)  // enum-type mismatch inside FluidSynth's own synth.h
#endif
#include <fluidsynth.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Master volume ceiling: 0..255 music volume maps to fluid_synth gain 0..FM_MAX_GAIN.
// Kept below 1.0 for headroom so busy SoundFonts don't clip. (Tunable.)
#define FM_MAX_GAIN 0.6f

static fluid_settings_t     *g_settings = NULL;
static fluid_synth_t        *g_synth = NULL;
static fluid_audio_driver_t *g_driver = NULL;
static SDL_Thread           *g_thread = NULL;
static bool                  g_sf_loaded = false;

static MidiSong  g_song;                 // owned by the player thread while running
static bool      g_loop = false;
static void    (*g_on_finish)(void) = NULL;

static SDL_atomic_t g_stop, g_paused, g_active, g_fade_ms, g_master_vol;
static double g_fade_scale = 1.0;        // thread-only: 1.0 normally, ramps to 0 on fade
static float  g_last_gain = -1.0f;       // last gain pushed to the synth (avoid redundant calls)

// From lds_play.c: the jukebox fades a song once it loops. The old SDL Mixer X path
// set this from its finished hook; we set it when the sequencer wraps.
extern bool songlooped;

// Called from the sequencer thread, or while that thread is stopped.

// Push the current master volume (scaled by any active fade) to the synth's gain.
static void fm_apply_gain(void)
{
	if (g_synth == NULL)
		return;
	int m = SDL_AtomicGet(&g_master_vol);
	float gain = FM_MAX_GAIN * ((float)m / 255.0f) * (float)g_fade_scale;
	if (gain < 0.0f)
		gain = 0.0f;
	if (gain != g_last_gain)
	{
		fluid_synth_set_gain(g_synth, gain);
		g_last_gain = gain;
	}
}

// Release every channel's notes (used at pause, stop, and the loop seam). Sounding
// notes get their note-off; instruments/controllers are left intact.
static void fm_all_notes_off(void)
{
	if (g_synth == NULL)
		return;
	for (int ch = 0; ch < 16; ++ch)
		fluid_synth_all_notes_off(g_synth, ch);
}

static void fm_send_event(const MidiEvent *e)
{
	if (e->status == 0xF0)  // SysEx
	{
		if (e->sx_len == 0)
			return;
		const Uint8 *d = g_song.sysex + e->sx_off;
		int len = (int)e->sx_len;
		// fluid_synth_sysex wants the payload without the leading 0xF0 / trailing 0xF7.
		if (len > 0 && d[0] == 0xF0) { ++d; --len; }
		if (len > 0 && d[len - 1] == 0xF7) { --len; }
		if (len > 0)
			fluid_synth_sysex(g_synth, (const char *)d, len, NULL, NULL, NULL, 0);
		return;
	}

	int ch = e->status & 0x0F;
	switch (e->status & 0xF0)
	{
	case 0x80: fluid_synth_noteoff(g_synth, ch, e->d1); break;
	case 0x90:
		if (e->d2 == 0) fluid_synth_noteoff(g_synth, ch, e->d1);  // note-on vel 0 == note-off
		else            fluid_synth_noteon(g_synth, ch, e->d1, e->d2);
		break;
	case 0xA0: fluid_synth_key_pressure(g_synth, ch, e->d1, e->d2); break;
	case 0xB0: fluid_synth_cc(g_synth, ch, e->d1, e->d2); break;      // CC0/CC32 bank handled internally
	case 0xC0: fluid_synth_program_change(g_synth, ch, e->d1); break;
	case 0xD0: fluid_synth_channel_pressure(g_synth, ch, e->d1); break;
	case 0xE0: fluid_synth_pitch_bend(g_synth, ch, e->d1 | (e->d2 << 7)); break;  // 14-bit, center 8192
	default: break;
	}
}

// Restart at the loop target (the loopStart marker, or the top if none): reset to a clean synth
// and re-establish the exact channel state the song had there.
static void fm_restore_loop_state(void)
{
	fm_all_notes_off();
	fluid_synth_system_reset(g_synth);  // programs->0, controllers/pitch->default

	size_t target = g_song.loop_marker ? g_song.loop_idx : 0;
	for (size_t i = 0; i < target; ++i)
	{
		Uint8 hi = g_song.events[i].status & 0xF0;
		if (hi == 0xB0 || hi == 0xC0 || hi == 0xE0 || g_song.events[i].status == 0xF0)
			fm_send_event(&g_song.events[i]);  // control change / program / pitch bend / sysex
	}
}

// Sequencer thread.

static int SDLCALL fm_thread(void *userdata)
{
	(void)userdata;

	SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);  // keep event timing tight under game load

	g_fade_scale = 1.0;
	g_last_gain = -1.0f;
	fluid_synth_system_reset(g_synth);  // clean baseline (the synth is reused across songs)
	fm_apply_gain();

	const Uint64 freq = SDL_GetPerformanceFrequency();
	Uint64 last = SDL_GetPerformanceCounter();
	Uint64 pos_us = 0;      // current playback position
	size_t ei = 0;          // next event to send
	bool silenced = false;  // are we currently holding notes off (paused)?

	bool   fading = false;
	Uint64 fade_accum_us = 0, fade_total_us = 0;

	for (;;)
	{
		if (SDL_AtomicGet(&g_stop))
			break;

		const Uint64 now = SDL_GetPerformanceCounter();
		const Uint64 dt_us = (now - last) * 1000000ULL / freq;
		last = now;

		if (SDL_AtomicGet(&g_paused))
		{
			if (!silenced) { fm_all_notes_off(); silenced = true; }
			SDL_Delay(5);
			continue;
		}
		silenced = false;

		// A completed fade ends the song through the normal one-shot path.
		const int req_fade = SDL_AtomicGet(&g_fade_ms);
		if (req_fade > 0)
		{
			fading = true;
			fade_total_us = (Uint64)req_fade * 1000ULL;
			fade_accum_us = 0;
			SDL_AtomicSet(&g_fade_ms, 0);
		}
		if (fading)
		{
			fade_accum_us += dt_us;
			if (fade_accum_us >= fade_total_us)
			{
				g_fade_scale = 0.0;
				fm_apply_gain();
				fm_all_notes_off();
				SDL_AtomicSet(&g_active, 0);
				if (g_on_finish) g_on_finish();
				break;
			}
			g_fade_scale = 1.0 - (double)fade_accum_us / (double)fade_total_us;
		}

		fm_apply_gain();  // pick up volume changes (and the fade ramp)

		pos_us += dt_us;

		while (ei < g_song.event_count && g_song.events[ei].time_us <= pos_us)
			fm_send_event(&g_song.events[ei++]);

		if (ei >= g_song.event_count)  // reached the end of the song
		{
			if (g_loop && !fading)
			{
				fm_restore_loop_state();  // reset + replay pre-target state (see above)
				ei = g_song.loop_marker ? g_song.loop_idx : 0;
				pos_us = g_song.loop_marker ? g_song.loop_us : 0;
				songlooped = true;  // as with OPL: the song has reached its loop
				continue;
			}
			fm_all_notes_off();
			SDL_AtomicSet(&g_active, 0);
			if (g_on_finish) g_on_finish();
			break;
		}

		SDL_Delay(1);
	}

	fm_all_notes_off();
	return 0;
}

// Public API.

bool fm_init(const char *soundfont, int sample_rate)
{
	if (g_synth != NULL)
		return true;  // already initialized

	static bool vol_defaulted = false;
	// Set the default only on the first open.
	if (!vol_defaulted) { SDL_AtomicSet(&g_master_vol, 255); vol_defaulted = true; }

	g_sf_loaded = false;
	if (sample_rate <= 0)
		sample_rate = 44100;

	g_settings = new_fluid_settings();
	if (g_settings == NULL)
	{
		fprintf(stderr, "fm: new_fluid_settings failed\n");
		return false;
	}

	fluid_settings_setnum(g_settings, "synth.sample-rate", (double)sample_rate);
	fluid_settings_setint(g_settings, "synth.threadsafe-api", 1);  // send events from our thread safely

	g_synth = new_fluid_synth(g_settings);
	if (g_synth == NULL)
	{
		fprintf(stderr, "fm: new_fluid_synth failed\n");
		delete_fluid_settings(g_settings); g_settings = NULL;
		return false;
	}

	if (soundfont != NULL && soundfont[0] != '\0')
	{
		if (fluid_synth_sfload(g_synth, soundfont, 1) != FLUID_FAILED)
		{
			g_sf_loaded = true;
			fluid_synth_program_reset(g_synth);  // channels pick up the new bank's presets
		}
		else
		{
			fprintf(stderr, "fm: could not load soundfont '%s'\n", soundfont);
		}
	}

	g_last_gain = -1.0f;
	fm_apply_gain();  // set initial gain from the master volume

	// FluidSynth spawns its own audio-render thread pulling from the synth. Try the
	// platform default first, then explicit Windows backends the DLL ships.
	g_driver = new_fluid_audio_driver(g_settings, g_synth);
	if (g_driver == NULL)
	{
		static const char *const drivers[] = { "wasapi", "dsound", "waveout" };
		for (size_t i = 0; i < SDL_arraysize(drivers) && g_driver == NULL; ++i)
		{
			fluid_settings_setstr(g_settings, "audio.driver", drivers[i]);
			g_driver = new_fluid_audio_driver(g_settings, g_synth);
		}
	}
	if (g_driver == NULL)
	{
		fprintf(stderr, "fm: could not open a FluidSynth audio driver\n");
		delete_fluid_synth(g_synth); g_synth = NULL;
		delete_fluid_settings(g_settings); g_settings = NULL;
		return false;
	}

	fprintf(stderr, "fm: FluidSynth ready (sr=%d, soundfont=%s%s)\n",
	        sample_rate,
	        (soundfont != NULL && soundfont[0] != '\0') ? soundfont : "(none)",
	        g_sf_loaded ? "" : "  [NOT LOADED]");
	return true;
}

void fm_stop(void)
{
	if (g_thread != NULL)
	{
		SDL_AtomicSet(&g_stop, 1);
		SDL_WaitThread(g_thread, NULL);  // returns promptly; the thread polls g_stop
		g_thread = NULL;
	}
	SDL_AtomicSet(&g_active, 0);
	if (g_synth != NULL)
		fm_all_notes_off();
	midi_smf_free(&g_song);
}

bool fm_play(const uint8_t *smf, size_t size, bool loop, void (*on_finish)(void))
{
	fm_stop();  // stop + join any current song and free it

	if (g_synth == NULL)
		return false;  // not initialized

	if (!midi_smf_parse(&g_song, smf, size))
	{
		fprintf(stderr, "fm: could not parse SMF (%zu bytes)\n", size);
		return false;
	}

	g_loop = loop;
	g_on_finish = on_finish;
	g_fade_scale = 1.0;
	g_last_gain = -1.0f;  // force a gain re-apply on the new thread
	SDL_AtomicSet(&g_stop, 0);
	SDL_AtomicSet(&g_paused, 0);
	SDL_AtomicSet(&g_fade_ms, 0);
	SDL_AtomicSet(&g_active, 1);

	g_thread = SDL_CreateThread(fm_thread, "fm_seq", NULL);
	if (g_thread == NULL)
	{
		SDL_AtomicSet(&g_active, 0);
		midi_smf_free(&g_song);
		fprintf(stderr, "fm: failed to start sequencer thread: %s\n", SDL_GetError());
		return false;
	}
	return true;
}

void fm_quit(void)
{
	fm_stop();
	if (g_driver != NULL) { delete_fluid_audio_driver(g_driver); g_driver = NULL; }
	if (g_synth != NULL) { delete_fluid_synth(g_synth); g_synth = NULL; }
	if (g_settings != NULL) { delete_fluid_settings(g_settings); g_settings = NULL; }
	g_sf_loaded = false;
	g_last_gain = -1.0f;
}

void fm_pause(void)  { SDL_AtomicSet(&g_paused, 1); }
void fm_resume(void) { SDL_AtomicSet(&g_paused, 0); }
bool fm_playing(void){ return SDL_AtomicGet(&g_active) != 0; }

void fm_fade_out(uint32_t ms)
{
	if (ms == 0) { fm_stop(); return; }
	if (fm_playing())
		SDL_AtomicSet(&g_fade_ms, (int)ms);
}

void fm_set_volume(uint8_t vol255)
{
	SDL_AtomicSet(&g_master_vol, vol255);  // the sequencer thread applies it as gain
}

bool fm_soundfont_loaded(void) { return g_sf_loaded; }

#else  /* !WITH_MIDI; no FluidSynth; loudness.c only calls these WITH_MIDI */

bool fm_init(const char *soundfont, int sample_rate) { (void)soundfont; (void)sample_rate; return false; }
void fm_quit(void) {}
bool fm_play(const uint8_t *smf, size_t size, bool loop, void (*on_finish)(void))
{
	(void)smf; (void)size; (void)loop; (void)on_finish; return false;
}
void fm_stop(void) {}
void fm_pause(void) {}
void fm_resume(void) {}
void fm_fade_out(uint32_t ms) { (void)ms; }
bool fm_playing(void) { return false; }
void fm_set_volume(uint8_t vol255) { (void)vol255; }
bool fm_soundfont_loaded(void) { return false; }

#endif /* WITH_MIDI */
