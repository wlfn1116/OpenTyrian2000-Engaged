/* Windows crash, fatal-error, and hang reporting. Other platforms use no-op stubs. */
#ifndef CRASHLOG_H
#define CRASHLOG_H

#include <stdbool.h>
#include <stdio.h>

void install_crash_handler(void);

// Report a fatal path that exits without raising an exception.
// Does not terminate; `detail` may be NULL. No-op outside Windows.
void crashlog_report_fatal(const char *event, const char *detail);

// Report a recovered problem without latching the reporter. Windows only.
void crashlog_note(const char *event, const char *detail);

// Write a detailed netplay report. Non-Windows builds omit the stack trace.
void crashlog_note_net(const char *event, const char *detail);

// Write a short timestamped netplay entry without context or a stack trace.
void crashlog_netlog_line(const char *event, const char *detail);

// Persisted Network Log switch. Off leaves existing files untouched.
void crashlog_set_netlog_enabled(bool enabled);
bool crashlog_get_netlog_enabled(void);

// Remove legacy fixed-name net logs. Call after config load and before netplay.
void crashlog_netlog_begin_session(void);

// Delete current and legacy opentyrian_*.log files. Not gated by the Network Log switch.
bool crashlog_clear_logs(void);

// Append the live game-state snapshot to an open report.
void crashlog_write_game_state(FILE *f);

// Current phase breadcrumb. phase must remain valid while a fault handler can read it.
void crashlog_set_phase(const char *phase);
const char *crashlog_get_phase(void);

// Main-thread hang watchdog. Initialize on the main thread and heartbeat once per event loop.
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
