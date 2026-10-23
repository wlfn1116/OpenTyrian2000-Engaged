/* Win32 native MIDI player implementation. See win_native_midi.h. */
#include "win_native_midi.h"

#ifdef _WIN32

#include "midi_smf.h"

#include "SDL.h"

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "winmm.lib")

static HMIDIOUT    g_hmo = NULL;
static SDL_Thread *g_thread = NULL;

static MidiSong    g_song;               // owned by the player thread while running
static bool        g_loop = false;
static void      (*g_on_finish)(void) = NULL;

static SDL_atomic_t g_stop, g_paused, g_active, g_vol_dirty, g_master_vol, g_fade_ms;

static Uint8  g_chan_vol[16];   // last song-set channel volume (CC7), pre-scaling
static double g_fade_scale = 1.0;  // thread-only: 1.0 normally, ramps to 0 on fade
static bool   g_period_set = false;

// Called from the player thread, or while that thread is stopped.

static void send_short(Uint8 status, Uint8 d1, Uint8 d2)
{
	if (g_hmo != NULL)
		midiOutShortMsg(g_hmo, (DWORD)status | ((DWORD)d1 << 8) | ((DWORD)d2 << 16));
}

// Apply master volume (and any active fade) to a raw 0..127 channel volume.
static Uint8 scaled_vol(int raw)
{
	int m = SDL_AtomicGet(&g_master_vol);
	double v = (double)raw * ((double)m / 255.0) * g_fade_scale;
	int iv = (int)(v + 0.5);
	if (iv < 0)   iv = 0;
	if (iv > 127) iv = 127;
	return (Uint8)iv;
}

// (Re)send CC7 (channel volume) to every channel so the master volume/fade takes
// effect even on channels the song never touched.
static void send_channel_volumes(void)
{
	for (int ch = 0; ch < 16; ++ch)
		send_short((Uint8)(0xB0 | ch), 7, scaled_vol(g_chan_vol[ch]));
}

static void all_notes_off(void)
{
	for (int ch = 0; ch < 16; ++ch)
	{
		send_short((Uint8)(0xB0 | ch), 120, 0);  // All Sound Off
		send_short((Uint8)(0xB0 | ch), 123, 0);  // All Notes Off
	}
}

// Put every channel into a clean, known state (used at song start and on loop).
static void reset_channels(void)
{
	for (int ch = 0; ch < 16; ++ch)
	{
		send_short((Uint8)(0xB0 | ch), 121, 0);  // Reset All Controllers
		send_short((Uint8)(0xB0 | ch), 123, 0);  // All Notes Off
		g_chan_vol[ch] = 100;                     // General MIDI default
	}
	send_channel_volumes();
}

static void send_event(const MidiEvent *e)
{
	if (e->status == 0xF0)  // SysEx
	{
		if (g_hmo == NULL || e->sx_len == 0)
			return;
		MIDIHDR hdr;
		memset(&hdr, 0, sizeof(hdr));
		hdr.lpData = (LPSTR)(g_song.sysex + e->sx_off);
		hdr.dwBufferLength = hdr.dwBytesRecorded = e->sx_len;
		if (midiOutPrepareHeader(g_hmo, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR)
		{
			midiOutLongMsg(g_hmo, &hdr, sizeof(hdr));
			// CALLBACK_NULL: the driver sets MHDR_DONE when finished with the buffer.
			for (int i = 0; i < 200 && !(hdr.dwFlags & MHDR_DONE); ++i)
				SDL_Delay(1);
			midiOutUnprepareHeader(g_hmo, &hdr, sizeof(hdr));
		}
		return;
	}

	// Channel volume (CC7) is scaled by the master volume; remember the raw value
	// so a later master-volume change can be re-applied on top of it.
	if ((e->status & 0xF0) == 0xB0 && e->d1 == 7)
	{
		g_chan_vol[e->status & 0x0F] = e->d2;
		send_short(e->status, 7, scaled_vol(e->d2));
		return;
	}

	send_short(e->status, e->d1, e->d2);
}

// Rebuild channel state before the loop target, skipping notes, so each loop starts identically.
static void restore_loop_state(void)
{
	all_notes_off();
	reset_channels();
	for (int ch = 0; ch < 16; ++ch)
		send_short((Uint8)(0xC0 | ch), 0, 0);  // program -> default (reset_channels doesn't reset it)

	size_t target = g_song.loop_marker ? g_song.loop_idx : 0;
	for (size_t i = 0; i < target; ++i)
	{
		Uint8 hi = g_song.events[i].status & 0xF0;
		if (hi == 0xB0 || hi == 0xC0 || hi == 0xE0 || g_song.events[i].status == 0xF0)
			send_event(&g_song.events[i]);  // control change / program / pitch bend / sysex
	}
}

// Player thread.

static int SDLCALL wnm_thread(void *userdata)
{
	(void)userdata;

	SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);  // keep event timing tight under game load

	g_fade_scale = 1.0;
	reset_channels();

	const Uint64 freq = SDL_GetPerformanceFrequency();
	Uint64 last = SDL_GetPerformanceCounter();
	Uint64 pos_us = 0;      // current playback position
	size_t ei = 0;          // next event to send
	bool silenced = false;  // are we currently holding notes off (paused)?

	bool   fading = false;
	Uint64 fade_accum_us = 0, fade_total_us = 0, fade_last_apply_us = 0;

	for (;;)
	{
		if (SDL_AtomicGet(&g_stop))
			break;

		const Uint64 now = SDL_GetPerformanceCounter();
		const Uint64 dt_us = (now - last) * 1000000ULL / freq;
		last = now;

		if (SDL_AtomicGet(&g_paused))
		{
			if (!silenced) { all_notes_off(); silenced = true; }
			SDL_Delay(5);
			continue;
		}
		silenced = false;

		if (SDL_AtomicGet(&g_vol_dirty))
		{
			send_channel_volumes();
			SDL_AtomicSet(&g_vol_dirty, 0);
		}

		// A completed fade ends the song through the normal one-shot path.
		const int req_fade = SDL_AtomicGet(&g_fade_ms);
		if (req_fade > 0)
		{
			fading = true;
			fade_total_us = (Uint64)req_fade * 1000ULL;
			fade_accum_us = 0;
			fade_last_apply_us = 0;
			SDL_AtomicSet(&g_fade_ms, 0);
		}
		if (fading)
		{
			fade_accum_us += dt_us;
			if (fade_accum_us >= fade_total_us)
			{
				all_notes_off();
				SDL_AtomicSet(&g_active, 0);
				if (g_on_finish) g_on_finish();
				break;
			}
			g_fade_scale = 1.0 - (double)fade_accum_us / (double)fade_total_us;
			if (fade_accum_us - fade_last_apply_us >= 25000)  // re-apply ~40x/sec
			{
				send_channel_volumes();
				fade_last_apply_us = fade_accum_us;
			}
		}

		pos_us += dt_us;

		while (ei < g_song.event_count && g_song.events[ei].time_us <= pos_us)
			send_event(&g_song.events[ei++]);

		if (ei >= g_song.event_count)  // reached the end of the song
		{
			if (g_loop && !fading)
			{
				restore_loop_state();  // reset + replay pre-target state (see above)
				ei = g_song.loop_marker ? g_song.loop_idx : 0;
				pos_us = g_song.loop_marker ? g_song.loop_us : 0;
				continue;
			}
			all_notes_off();
			SDL_AtomicSet(&g_active, 0);
			if (g_on_finish) g_on_finish();
			break;
		}

		SDL_Delay(1);
	}

	all_notes_off();
	return 0;
}

// Public API.

bool wnm_init(void)
{
	if (g_hmo != NULL)
		return true;
	static bool vol_defaulted = false;
	// Set the default only on the first open.
	if (!vol_defaulted) { SDL_AtomicSet(&g_master_vol, 255); vol_defaulted = true; }
	MMRESULT r = midiOutOpen(&g_hmo, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
	if (r != MMSYSERR_NOERROR)
	{
		g_hmo = NULL;
		fprintf(stderr, "wnm: midiOutOpen failed (%u)\n", (unsigned)r);
		return false;
	}
	if (!g_period_set) { timeBeginPeriod(1); g_period_set = true; }  // 1 ms scheduler
	fprintf(stderr, "wnm: native MIDI player ready (built-in, MIDI mapper)\n");
	return true;
}

// stop and silence (safe; never blocks on winmm)
static void wnm_stop(void)
{
	if (g_thread != NULL)
	{
		SDL_AtomicSet(&g_stop, 1);
		SDL_WaitThread(g_thread, NULL);  // returns promptly; the thread polls g_stop
		g_thread = NULL;
	}
	SDL_AtomicSet(&g_active, 0);
	if (g_hmo != NULL)
		all_notes_off();
	midi_smf_free(&g_song);
}

bool wnm_play(const uint8_t *smf, size_t size, bool loop, void (*on_finish)(void))
{
	wnm_stop();  // stop + join any current song and free it

	if (g_hmo == NULL && !wnm_init())
		return false;

	if (!midi_smf_parse(&g_song, smf, size))
	{
		fprintf(stderr, "wnm: could not parse SMF (%zu bytes)\n", size);
		return false;
	}

	g_loop = loop;
	g_on_finish = on_finish;
	g_fade_scale = 1.0;
	SDL_AtomicSet(&g_stop, 0);
	SDL_AtomicSet(&g_paused, 0);
	SDL_AtomicSet(&g_vol_dirty, 0);
	SDL_AtomicSet(&g_fade_ms, 0);
	SDL_AtomicSet(&g_active, 1);

	g_thread = SDL_CreateThread(wnm_thread, "wnm_midi", NULL);
	if (g_thread == NULL)
	{
		SDL_AtomicSet(&g_active, 0);
		midi_smf_free(&g_song);
		fprintf(stderr, "wnm: failed to start player thread: %s\n", SDL_GetError());
		return false;
	}
	return true;
}

void wnm_quit(void)
{
	wnm_stop();
	if (g_hmo != NULL)
	{
		midiOutReset(g_hmo);
		midiOutClose(g_hmo);
		g_hmo = NULL;
	}
	if (g_period_set) { timeEndPeriod(1); g_period_set = false; }
}

void wnm_pause(void)  { SDL_AtomicSet(&g_paused, 1); }
void wnm_resume(void) { SDL_AtomicSet(&g_paused, 0); }
// true while a song is actively playing
static bool wnm_playing(void){ return SDL_AtomicGet(&g_active) != 0; }

void wnm_fade_out(uint32_t ms)
{
	if (ms == 0) { wnm_stop(); return; }
	if (wnm_playing())
		SDL_AtomicSet(&g_fade_ms, (int)ms);
}

void wnm_set_volume(uint8_t vol255)
{
	SDL_AtomicSet(&g_master_vol, vol255);
	SDL_AtomicSet(&g_vol_dirty, 1);
}

#else  /* !_WIN32; no native MIDI; audio.c only calls these on Windows */

bool wnm_init(void) { return false; }
void wnm_quit(void) {}
bool wnm_play(const uint8_t *smf, size_t size, bool loop, void (*on_finish)(void))
{
	(void)smf; (void)size; (void)loop; (void)on_finish; return false;
}
void wnm_stop(void) {}
void wnm_pause(void) {}
void wnm_resume(void) {}
void wnm_fade_out(uint32_t ms) { (void)ms; }
bool wnm_playing(void) { return false; }
void wnm_set_volume(uint8_t vol255) { (void)vol255; }

#endif /* _WIN32 */
