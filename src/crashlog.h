/* Windows crash, fatal-error, and hang reporting. Other platforms use no-op stubs. */
#ifndef CRASHLOG_H
#define CRASHLOG_H

#include <stdbool.h>
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

// Same report format, but written to the NETWORK log (log/opentyrian_net_<launch time>.log, this
// session's own log, created on its first entry) instead of the crash log. For netplay health
// events -- desyncs, stalls, resyncs -- which are link/session trouble rather than process
// failures, so a lossy session can't bury a real crash report. All platforms: consoles write a
// reduced entry (header + detail, no stack) to their net log in the user directory.
void crashlog_note_net(const char *event, const char *detail);

// Short timestamped net-log entry with no context/stack body. For session bookkeeping (the
// start/end banners around an online session), so the log always shows logging is alive and an
// entry-free session reads as "no trouble detected", not "never ran". All platforms.
void crashlog_netlog_line(const char *event, const char *detail);

// Net-log master switch (menu: Setup -> Network Log, persisted in the config). Off means nothing
// touches the net logs at all: no entries, no file created, and existing logs left where they
// are. On by default; config.c applies the saved value at load, after the handlers are installed.
void crashlog_set_netlog_enabled(bool enabled);
bool crashlog_get_netlog_enabled(void);

// Sweep the fixed-name / numbered net logs (opentyrian_net.log, opentyrian_net.N.log) older builds
// kept loose beside the executable, which were rewritten each launch by contract. Nothing is
// reserved for this session: its log is log/opentyrian_net_<launch time>.log and comes into
// existence only on the first entry -- folder included -- so a session with nothing to report
// leaves no file and no earlier log is ever overwritten. Call once from main() AFTER the config is
// read, so the switch above is the saved one, and before any netplay. No-op while the switch is off.
void crashlog_netlog_begin_session(void);

// Delete every log this game has written -- crash and net alike, this session's included, matched
// on the shared opentyrian_*.log naming so nothing else in the directory is touched. Behind the
// consoles' "Clear Logs" row, where there is no file manager to prune them with. Returns true if
// at least one was deleted, false if there were none. Not gated on the net-log switch: logs
// already written stay reachable after it is turned off.
bool crashlog_clear_logs(void);

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
// If the heartbeat stalls, the watchdog writes the stuck main-thread stack to this session's log.
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
