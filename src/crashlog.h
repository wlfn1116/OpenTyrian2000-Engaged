/* Windows crash, fatal-error, and hang reporting. Other platforms use no-op stubs. */
#ifndef CRASHLOG_H
#define CRASHLOG_H

#include <stdio.h>

void install_crash_handler(void);

// Report a fatal path that exits without raising an exception.
// Does not terminate; `detail` may be NULL. No-op outside Windows.
void crashlog_report_fatal(const char *event, const char *detail);

// Write a report for a RECOVERED problem -- something that would have crashed but was caught and
// handled (e.g. an out-of-range level index in a desynced save, steered back to the title instead
// of running off the end of the level file). Same rich context/stack as a crash, but does NOT
// latch: the session continues, so a genuinely fatal crash later still logs. Windows only.
void crashlog_note(const char *event, const char *detail);

// Same report format, but written to the NETWORK log (opentyrian_net.log, rotated alongside the
// crash log) instead of the crash log. For netplay health events -- desyncs, stalls, resyncs --
// which are link/session trouble rather than process failures, so a lossy session can't bury a
// real crash report. Windows only.
void crashlog_note_net(const char *event, const char *detail);

// Append the live game-state snapshot to an open crash report. Defined in crashlog_state.c
// (its own <windows.h>-free TU) and invoked by the crash/hang/CRT-fatal paths. Reads only
// static globals and never faults on a corrupt process; safe to call from a fault handler.
void crashlog_write_game_state(FILE *f);

// "Current phase" breadcrumb. Game code calls crashlog_set_phase() at coarse phase boundaries
// (title, shop, loading a level, in gameplay, cutscene); the crash report prints the last one
// set, giving a bare/unsymbolised stack trace context. The pointer must be a string literal or
// otherwise long-lived string -- it is read from the fault handler. Both are defined in
// crashlog_state.c; set is a trivial global store, safe and cheap to call from anywhere.
void crashlog_set_phase(const char *phase);
const char *crashlog_get_phase(void);

// Hang watchdog: catches a hard main-thread hang (infinite loop / deadlock) that the crash
// handler can't (no exception fires). Call watchdog_init() once at startup ON THE MAIN THREAD,
// and watchdog_heartbeat() from a spot the main loop hits every iteration (service_SDL_events).
// If the heartbeat stalls, the watchdog writes the stuck main-thread stack to opentyrian_log.log.
// Windows only; a no-op elsewhere.
void watchdog_init(void);
void watchdog_heartbeat(void);

// Main-thread stall threshold in seconds. Low values can report slow loads as hangs.
#define CRASHLOG_HANG_TIMEOUT_MIN     1     // 1s granularity floor (the watchdog polls every ~1s)
#define CRASHLOG_HANG_TIMEOUT_MAX     9999  // matches the 4-digit debug-menu input field; ~2.7h = "off"
#define CRASHLOG_HANG_TIMEOUT_DEFAULT 5
void crashlog_set_hang_timeout(int seconds);  // clamps to [MIN, MAX]
int  crashlog_get_hang_timeout(void);

#endif // CRASHLOG_H
