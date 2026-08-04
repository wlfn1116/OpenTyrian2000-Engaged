/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Crash / diagnostic logger — see crashlog.h.
 */
#include "crashlog.h"

#ifdef _WIN32

#include <windows.h>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4255)  // unprototyped callback typedefs inside the SDK's own dbghelp.h
#endif
#include <dbghelp.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <tlhelp32.h>
#include <psapi.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

// Two logs, written to a "log" folder next to the executable (created on demand).  The crash log
// holds every kind of hard process failure (exception, hang, abort, CRT fatal); the net log holds
// netplay health events (desyncs, stalls, resyncs) so they can't bury a real crash report.
// Neither file exists until there is something to put in it, and each is named for the launch it
// belongs to -- log\opentyrian_log_2026-08-04_143012.log -- so one session writes at most one of
// each, a session with nothing to report leaves none at all, and no report is ever overwritten by
// a later run.  Nothing is rotated and nothing is deleted: the timestamp in the name is the
// history, and the folder is what keeps that history from silting up the game directory.
#define LOG_DIR     "log"
#define LOG_PREFIX  "opentyrian_"        // what every log this game writes starts with
#define LOG_STEM    LOG_PREFIX "log"
#define NETLOG_STEM LOG_PREFIX "net"

// Enough for "<stem>_YYYY-MM-DD_HHMMSS.log" with room to spare.
#define LOG_NAME_MAX 64

// The numbered generations older builds kept beside each live log (opentyrian_net.1.log ... .N.log).
// Only the net log's are swept, and only once at startup: those were rewritten every launch by
// contract, so dropping them is what the build that wrote them would have done. The crash chain is
// left where it is -- those are real reports somebody may still want.
#define LEGACY_NETLOG_KEEP 3

extern const char *opentyrian_str;      // opentyr.c
extern const char *opentyrian_version;  // opentyr.c
extern const char *opentyrian_commit;   // opentyr.c

// Guards the terminal fault paths (exception / abort / CRT fatal) so a fault raised mid-report
// can't re-enter the logger and clobber it. The hang watchdog is non-terminal and stays
// re-armable, so it doesn't use this.
static volatile LONG s_reporting = 0;

// Set once the net logs older builds left behind have been swept this run.
static volatile LONG s_legacyNetLogsSwept = 0;

// Net-log master switch (Setup -> Network Log); see crashlog.h.
static volatile LONG s_netLogEnabled = 1;

// Last fault the vectored handler reported; the backup filter skips exactly this (code, addr)
// pair. Not a latch, only the most recent fault is held.
static volatile DWORD s_reportedCode = 0;
static volatile PVOID s_reportedAddr = NULL;

// Captured once at install time on the main thread, so every report can show session uptime
// and tell whether the faulting thread is the main thread or a background one (SDL audio, etc.).
static DWORD     s_mainThreadId = 0;
static ULONGLONG s_startTick    = 0;

// "YYYY-MM-DD_HHMMSS" for this launch, fixed once so every report of a session lands in the same
// pair of files. Built at install time on the main thread, before any handler is armed.
static char s_sessionStamp[24];

// Serializes the single-threaded dbghelp session (Sym*/StackWalk64) so a crash walk and a hang
// walk can't corrupt each other. Recursive, so a fault while held can still re-enter and report.
static CRITICAL_SECTION s_dbghelpLock;

// Build "<exe dir>\<filename>".
static void log_path(char *out, size_t outSize, const char *filename)
{
	char exePath[MAX_PATH];
	DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
	if (len == 0 || len >= MAX_PATH)
	{
		snprintf(out, outSize, "%s", filename);
		return;
	}
	char *slash = strrchr(exePath, '\\');
	if (slash != NULL)
		*(slash + 1) = '\0';
	snprintf(out, outSize, "%s%s", exePath, filename);
}

// Fix the session stamp. Called at install time; the lazy call in log_filename covers a report
// raised before install_crash_handler ran -- there shouldn't be one, but an unnamed log is worse.
static void build_session_stamp(void)
{
	time_t now = time(NULL);
	struct tm lt;
	if (localtime_s(&lt, &now) != 0 ||
	    strftime(s_sessionStamp, sizeof(s_sessionStamp), "%Y-%m-%d_%H%M%S", &lt) == 0)
		snprintf(s_sessionStamp, sizeof(s_sessionStamp), "unknown");
}

// "<stem>_<session stamp>.log" -- the same name for every report of this launch.
static void log_filename(char *out, size_t outSize, const char *stem)
{
	if (s_sessionStamp[0] == '\0')
		build_session_stamp();
	snprintf(out, outSize, "%s_%s.log", stem, s_sessionStamp);
}

// The roots a log folder may be placed under, in the order they're tried: next to the exe, the
// working directory, then %TEMP%. The last two are fallbacks for a read-only install directory.
// Each ends in a separator. Returns how many were filled in.
static int log_roots(char roots[3][MAX_PATH])
{
	int count = 0;

	log_path(roots[count], MAX_PATH, "");  // exe dir, trailing backslash kept
	if (roots[count][0] != '\0')
		++count;

	snprintf(roots[count++], MAX_PATH, ".\\");

	DWORD n = GetTempPathA(MAX_PATH, roots[count]);
	if (n > 0 && n < MAX_PATH)
		++count;

	return count;
}

// "<root>log\" -- the writing form, which creates the folder if it isn't there. False if it can't
// be created, which is the signal to fall through to the next root.
static bool make_log_dir(const char *root, char *out, size_t outSize)
{
	char dir[MAX_PATH];
	snprintf(dir, sizeof(dir), "%s%s", root, LOG_DIR);

	if (!CreateDirectoryA(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
		return false;

	snprintf(out, outSize, "%s\\", dir);
	return true;
}

// "<root>log\" without creating anything -- for the delete/sweep paths, which must not conjure an
// empty folder in a root this install never writes to.
static void log_dir_path(const char *root, char *out, size_t outSize)
{
	snprintf(out, outSize, "%s%s\\", root, LOG_DIR);
}

// Open a log in the first root whose log folder can be made and written to. NULL only if none can.
// Always appends: the name carries the launch time, so there is never a stale file to clear out,
// and two instances started within the same second stack their reports rather than one truncating
// the other's.
static FILE *open_log_file(const char *filename)
{
	char roots[3][MAX_PATH];
	int count = log_roots(roots);

	for (int i = 0; i < count; ++i)
	{
		char dir[MAX_PATH];
		if (!make_log_dir(roots[i], dir, sizeof(dir)))
			continue;

		char path[MAX_PATH + LOG_NAME_MAX];
		snprintf(path, sizeof(path), "%s%s", dir, filename);

		FILE *f = fopen(path, "a");
		if (f != NULL)
			return f;
	}
	return NULL;
}

static FILE *open_log(void)
{
	char name[LOG_NAME_MAX];
	log_filename(name, sizeof(name), LOG_STEM);
	return open_log_file(name);
}

static FILE *open_net_log(void)
{
	char name[LOG_NAME_MAX];
	log_filename(name, sizeof(name), NETLOG_STEM);
	return open_log_file(name);
}

// Delete every "<prefix>*.log" in `dir`, returning how many went. Matching on the game's own
// filename prefix rather than a bare *.log is what makes this safe to point at a shared directory
// (a %TEMP% fallback, the working directory): nothing another program wrote can match.
static int delete_logs_in(const char *dir, const char *prefix)
{
	char pattern[MAX_PATH + LOG_NAME_MAX];
	snprintf(pattern, sizeof(pattern), "%s%s*.log", dir, prefix);

	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return 0;

	int deleted = 0;
	do
	{
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;

		char path[MAX_PATH + LOG_NAME_MAX];
		snprintf(path, sizeof(path), "%s%s", dir, fd.cFileName);
		if (DeleteFileA(path))
			++deleted;
	} while (FindNextFileA(h, &fd));

	FindClose(h);
	return deleted;
}

// Remove the fixed-name and numbered net logs older builds left behind, once per run, so a stale
// opentyrian_net.log can't be mistaken for one of this build's timestamped ones. Those predate the
// log folder, so they sit loose in the roots. Only the net log's -- see LEGACY_NETLOG_KEEP.
static void sweep_legacy_net_logs(void)
{
	if (InterlockedExchange(&s_legacyNetLogsSwept, 1) != 0)
		return;

	char roots[3][MAX_PATH];
	int count = log_roots(roots);
	for (int i = 0; i < count; ++i)
	{
		char path[MAX_PATH + LOG_NAME_MAX];

		snprintf(path, sizeof(path), "%s%s.log", roots[i], NETLOG_STEM);
		DeleteFileA(path);  // absent / locked -> fails harmlessly

		for (int n = 1; n <= LEGACY_NETLOG_KEEP; ++n)
		{
			snprintf(path, sizeof(path), "%s%s.%d.log", roots[i], NETLOG_STEM, n);
			DeleteFileA(path);
		}
	}
}

void crashlog_set_netlog_enabled(bool enabled)
{
	InterlockedExchange(&s_netLogEnabled, enabled ? 1 : 0);
}

bool crashlog_get_netlog_enabled(void)
{
	return s_netLogEnabled != 0;
}

// This session's net log names itself, so there is nothing to reserve or clear out; all this does
// is drop the leftovers of older builds. Called after the config load, late enough for the master
// switch to be the saved one: off still means untouched.
void crashlog_netlog_begin_session(void)
{
	if (!crashlog_get_netlog_enabled())
		return;

	sweep_legacy_net_logs();
}

// Delete every log this game has written -- crash and net alike, this session's included: each
// root's log folder, plus the root itself for the loose ones older builds wrote. Returns true if
// at least one went; false means there were none. A later entry in this session simply starts its
// log over under the same name -- open_log_file remakes the folder if this took it down to empty.
bool crashlog_clear_logs(void)
{
	char roots[3][MAX_PATH];
	int count = log_roots(roots);

	int deleted = 0;
	for (int i = 0; i < count; ++i)
	{
		char dir[MAX_PATH];
		log_dir_path(roots[i], dir, sizeof(dir));

		deleted += delete_logs_in(dir, LOG_PREFIX);
		deleted += delete_logs_in(roots[i], LOG_PREFIX);
	}

	return deleted > 0;
}

// Human-readable name for a Windows structured-exception code.
static const char *exception_name(DWORD code)
{
	switch (code)
	{
	case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
	case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
	case EXCEPTION_FLT_DENORMAL_OPERAND:     return "FLT_DENORMAL_OPERAND";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
	case EXCEPTION_FLT_INEXACT_RESULT:       return "FLT_INEXACT_RESULT";
	case EXCEPTION_FLT_INVALID_OPERATION:    return "FLT_INVALID_OPERATION";
	case EXCEPTION_FLT_OVERFLOW:             return "FLT_OVERFLOW";
	case EXCEPTION_FLT_STACK_CHECK:          return "FLT_STACK_CHECK";
	case EXCEPTION_FLT_UNDERFLOW:            return "FLT_UNDERFLOW";
	case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
	case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
	case EXCEPTION_INT_OVERFLOW:             return "INT_OVERFLOW";
	case EXCEPTION_INVALID_DISPOSITION:      return "INVALID_DISPOSITION";
	case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
	case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
	case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
	case EXCEPTION_BREAKPOINT:               return "BREAKPOINT";
	case EXCEPTION_SINGLE_STEP:              return "SINGLE_STEP";
	case 0xC0000374:                         return "HEAP_CORRUPTION";
	case 0xC0000409:                         return "STACK_BUFFER_OVERRUN";
	case 0xE06D7363:                         return "C++ exception";
	default:                                 return "unknown";
	}
}

// Banner shared by every report: what happened, when, and which build.
static void write_header(FILE *f, const char *event)
{
	time_t now = time(NULL);
	struct tm lt;
	char when[64] = "unknown";
	if (localtime_s(&lt, &now) == 0)
		strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &lt);

	fprintf(f, "================================================================\n");
	fprintf(f, "%s %s\n", opentyrian_str, event);
	fprintf(f, "  Version:     %s\n", opentyrian_version ? opentyrian_version : "?");
	fprintf(f, "  Commit:      %s\n", (opentyrian_commit && *opentyrian_commit) ? opentyrian_commit : "?");
	fprintf(f, "  Time:        %s\n", when);
	fprintf(f, "  Module base: %p\n", (void *)GetModuleHandleA(NULL));
	fprintf(f, "================================================================\n\n");
}

// Process-level context under the header: game phase, session uptime, which thread faulted (main
// vs. a background/audio thread), and memory use. `faultTid` is the current thread on the
// crash/CRT-fatal paths, or the stalled main thread for the hang watchdog.
static void write_process_info(FILE *f, DWORD faultTid)
{
	fprintf(f, "Phase:       %s\n", crashlog_get_phase() ? crashlog_get_phase() : "?");

	ULONGLONG up = GetTickCount64() - s_startTick;
	fprintf(f, "Uptime:      %llu.%03llu s\n",
	        (unsigned long long)(up / 1000), (unsigned long long)(up % 1000));

	fprintf(f, "Thread:      %lu  %s\n", (unsigned long)faultTid,
	        faultTid == s_mainThreadId ? "(main thread)"
	                                   : "(NOT main -- background/SDL audio/worker thread)");

	PROCESS_MEMORY_COUNTERS pmc;
	memset(&pmc, 0, sizeof(pmc));
	pmc.cb = sizeof(pmc);
	if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
		fprintf(f, "Memory:      working set %.1f MB (peak %.1f MB)\n",
		        pmc.WorkingSetSize / 1048576.0, pmc.PeakWorkingSetSize / 1048576.0);

	fprintf(f, "\n");
}

// Decode an exception record: code + name, faulting instruction address, and (for access
// violations / in-page errors) whether it was a read/write/execute and at what address.
static void write_exception_details(FILE *f, const EXCEPTION_RECORD *er)
{
	HMODULE base = GetModuleHandleA(NULL);
	void *faultAddr = er->ExceptionAddress;

	fprintf(f, "Exception:   0x%08lX (%s)\n",
	        (unsigned long)er->ExceptionCode, exception_name(er->ExceptionCode));
	fprintf(f, "Fault at:    %p  (RVA 0x%llX)\n", faultAddr,
	        (unsigned long long)((char *)faultAddr - (char *)base));

	if ((er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
	     er->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
	    er->NumberParameters >= 2)
	{
		ULONG_PTR op = er->ExceptionInformation[0];
		const char *what = (op == 0) ? "reading"
		                 : (op == 1) ? "writing"
		                 : (op == 8) ? "executing"
		                             : "accessing";
		fprintf(f, "Bad access:  %s address %p\n",
		        what, (void *)er->ExceptionInformation[1]);
	}
	fprintf(f, "\n");
}

// Register snapshot (read-only; taken before StackWalk64 mutates the context).
static void write_registers(FILE *f, const CONTEXT *c)
{
	fprintf(f, "Registers:\n");
#if defined(_M_X64)
	fprintf(f, "  RAX=%016llX RBX=%016llX RCX=%016llX RDX=%016llX\n",
	        (unsigned long long)c->Rax, (unsigned long long)c->Rbx,
	        (unsigned long long)c->Rcx, (unsigned long long)c->Rdx);
	fprintf(f, "  RSI=%016llX RDI=%016llX RBP=%016llX RSP=%016llX\n",
	        (unsigned long long)c->Rsi, (unsigned long long)c->Rdi,
	        (unsigned long long)c->Rbp, (unsigned long long)c->Rsp);
	fprintf(f, "  R8 =%016llX R9 =%016llX R10=%016llX R11=%016llX\n",
	        (unsigned long long)c->R8, (unsigned long long)c->R9,
	        (unsigned long long)c->R10, (unsigned long long)c->R11);
	fprintf(f, "  R12=%016llX R13=%016llX R14=%016llX R15=%016llX\n",
	        (unsigned long long)c->R12, (unsigned long long)c->R13,
	        (unsigned long long)c->R14, (unsigned long long)c->R15);
	fprintf(f, "  RIP=%016llX EFL=%08lX\n",
	        (unsigned long long)c->Rip, (unsigned long)c->EFlags);
#else
	fprintf(f, "  EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX\n",
	        (unsigned long)c->Eax, (unsigned long)c->Ebx,
	        (unsigned long)c->Ecx, (unsigned long)c->Edx);
	fprintf(f, "  ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX\n",
	        (unsigned long)c->Esi, (unsigned long)c->Edi,
	        (unsigned long)c->Ebp, (unsigned long)c->Esp);
	fprintf(f, "  EIP=%08lX EFL=%08lX\n",
	        (unsigned long)c->Eip, (unsigned long)c->EFlags);
#endif
	fprintf(f, "\n");
}

// Walk `thr`'s call stack (using `ctx` as the starting register state) and write a
// symbolised trace to `f`. The caller owns SymInitialize/SymCleanup so this can run inside
// either the crash handler or the hang watchdog. Mutates `ctx` (StackWalk64 advances it).
static void write_stack_trace(FILE *f, HANDLE proc, HANDLE thr, CONTEXT *ctx)
{
	STACKFRAME64 frame;
	memset(&frame, 0, sizeof(frame));
	DWORD machine;
#if defined(_M_X64)
	machine = IMAGE_FILE_MACHINE_AMD64;
	frame.AddrPC.Offset    = ctx->Rip;
	frame.AddrFrame.Offset = ctx->Rbp;
	frame.AddrStack.Offset = ctx->Rsp;
#else
	machine = IMAGE_FILE_MACHINE_I386;
	frame.AddrPC.Offset    = ctx->Eip;
	frame.AddrFrame.Offset = ctx->Ebp;
	frame.AddrStack.Offset = ctx->Esp;
#endif
	frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;

	fprintf(f, "Stack trace:\n");
	for (int i = 0; i < 48; ++i)
	{
		if (!StackWalk64(machine, proc, thr, &frame, ctx, NULL,
		                 SymFunctionTableAccess64, SymGetModuleBase64, NULL))
			break;

		DWORD64 addr = frame.AddrPC.Offset;
		if (addr == 0)
			break;

		DWORD64 modBase = SymGetModuleBase64(proc, addr);
		fprintf(f, "  %2d: [rva 0x%llX] ", i,
		        (unsigned long long)(modBase ? addr - modBase : addr));

		char symBuf[sizeof(SYMBOL_INFO) + 256];
		memset(symBuf, 0, sizeof(symBuf));
		SYMBOL_INFO *sym = (SYMBOL_INFO *)symBuf;
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = 255;
		DWORD64 disp = 0;
		if (SymFromAddr(proc, addr, &disp, sym))
			fprintf(f, "%s + 0x%llX", sym->Name, (unsigned long long)disp);
		else
			fprintf(f, "0x%llX", (unsigned long long)addr);

		IMAGEHLP_LINE64 line;
		memset(&line, 0, sizeof(line));
		line.SizeOfStruct = sizeof(line);
		DWORD lineDisp = 0;
		if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line))
			fprintf(f, "  (%s:%lu)", line.FileName, (unsigned long)line.LineNumber);

		fprintf(f, "\n");
	}
}

// List loaded modules with their address range, so an RVA (or a fault inside a DLL such as
// SDL2) can be attributed to the right binary even without a .pdb.
static void write_modules(FILE *f)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
	if (snap == INVALID_HANDLE_VALUE)
		return;

	MODULEENTRY32W me;
	memset(&me, 0, sizeof(me));
	me.dwSize = sizeof(me);

	fprintf(f, "\nLoaded modules:\n");
	if (Module32FirstW(snap, &me))
	{
		int n = 0;
		do
		{
			fprintf(f, "  %p - %p  %ls\n",
			        (void *)me.modBaseAddr,
			        (void *)(me.modBaseAddr + me.modBaseSize),
			        me.szModule);
		} while (Module32NextW(snap, &me) && ++n < 128);
	}
	CloseHandle(snap);
}

// Registers + symbolised stack + game-state snapshot + module list for thread `thr`, shared by
// every reporting path. Owns the dbghelp symbol session. The game state is written after the
// stack walk (the most fault-prone step) so a corrupt process still yields the trace; each stage
// is flushed to disk first.
static void write_context_report(FILE *f, HANDLE thr, CONTEXT *ctx)
{
	HANDLE proc = GetCurrentProcess();
	write_registers(f, ctx);

	// Flush everything so far to disk before the symbol session below. The stack walk can itself
	// fault on a corrupt process; the re-entry guard would then abort the nested report and fclose
	// never runs, losing whatever is still buffered (including the decoded fault address).
	fflush(f);

	// dbghelp is single-threaded: serialize the symbol session so a background-thread crash and the
	// hang watchdog's walk can't run it concurrently. The lock covers only the Sym* section; the
	// game-state dump and write_modules are thread-safe and stay outside it.
	EnterCriticalSection(&s_dbghelpLock);
	SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
	SymInitialize(proc, NULL, TRUE);
	write_stack_trace(f, proc, thr, ctx);
	SymCleanup(proc);
	LeaveCriticalSection(&s_dbghelpLock);

	fflush(f);
	crashlog_write_game_state(f);
	write_modules(f);
}

// Unhandled structured exceptions.

// Codes that mean a genuine crash. The vectored handler reports on these; the game and SDL never
// handle them first-chance, so it doesn't fire spuriously, and the writer doesn't latch, so a rare
// handled first-chance fault can't suppress a later real crash.
static bool is_fatal_exception(DWORD code)
{
	switch (code)
	{
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION:
	case EXCEPTION_STACK_OVERFLOW:
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
	case EXCEPTION_DATATYPE_MISALIGNMENT:
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
	case 0xC0000374:  // heap corruption (bypasses the top-level filter)
	case 0xC0000409:  // stack buffer overrun / __fastfail (bypasses it too)
		return true;
	default:
		return false;
	}
}

// Write the full crash report for `ep`. s_reporting guards against re-entry if the report itself
// faults; it is reset on the way out (not latched), so a handled first-chance fatal can't suppress
// a later real crash. On unhandled paths the process dies right after, so the reset is moot.
static void write_crash_report(EXCEPTION_POINTERS *ep, const char *event)
{
	if (InterlockedExchange(&s_reporting, 1) != 0)
		return;

	FILE *f = open_log();
	if (f != NULL)
	{
		write_header(f, event);
		write_process_info(f, GetCurrentThreadId());
		write_exception_details(f, ep->ExceptionRecord);
		write_context_report(f, GetCurrentThread(), ep->ContextRecord);
		fclose(f);

		// Record this fault so the top-level backup filter (crash_handler), firing next on the same
		// exception, recognises it and won't overwrite this report.
		s_reportedCode = ep->ExceptionRecord->ExceptionCode;
		s_reportedAddr = ep->ExceptionRecord->ExceptionAddress;
	}

	InterlockedExchange(&s_reporting, 0);
}

// Vectored handler: runs for every exception, ahead of frame handlers and the top-level filter, so
// it catches faults even when that filter is bypassed (reset by another lib, an upstream __try,
// some debugger setups). Reports on fatal codes and returns CONTINUE_SEARCH, so the process still
// dies exactly as it would have.
static LONG WINAPI crash_veh(EXCEPTION_POINTERS *ep)
{
	if (is_fatal_exception(ep->ExceptionRecord->ExceptionCode))
		write_crash_report(ep, "CRASH (fatal exception)");

	return EXCEPTION_CONTINUE_SEARCH;  // don't swallow; let normal termination proceed
}

// Top-level filter: backup for anything the vectored handler didn't report. On the same exception
// its second-chance context often points at thread start, so re-writing would replace the good
// trace with a useless 1-frame one; skip that exact (code, addr) pair, but still log a genuinely
// different fault the vectored handler missed.
static LONG WINAPI crash_handler(EXCEPTION_POINTERS *ep)
{
	const EXCEPTION_RECORD *er = ep->ExceptionRecord;
	if (er->ExceptionCode != s_reportedCode || er->ExceptionAddress != s_reportedAddr)
		write_crash_report(ep, "CRASH (unhandled exception)");
	return EXCEPTION_EXECUTE_HANDLER;  // let the process terminate normally
}

// CRT-level fatal errors.
// These terminate the process without raising an SEH exception, so crash_handler never sees
// them. Each hook captures the current context and writes the same rich report, then exits.

// Shared body: capture this thread's context and write a full report. Used by the CRT-fatal
// hooks (which then _exit) and by crashlog_report_fatal (which returns to its caller's own exit).
// `net` selects the net log over the crash log (crashlog_note_net).
// Returns true if it wrote a report, false if another report is already in progress (re-entry).
static bool write_captured_report_ex(bool net, const char *event, const char *detail)
{
	if (InterlockedExchange(&s_reporting, 1) != 0)
		return false;

	FILE *f = net ? open_net_log() : open_log();
	if (f != NULL)
	{
		CONTEXT ctx;
		memset(&ctx, 0, sizeof(ctx));
		RtlCaptureContext(&ctx);  // capture this thread's state at the point of failure

		write_header(f, event);
		write_process_info(f, GetCurrentThreadId());
		if (detail != NULL)
			fprintf(f, "%s\n\n", detail);
		write_context_report(f, GetCurrentThread(), &ctx);
		fclose(f);
	}

	InterlockedExchange(&s_reporting, 0);
	return true;
}

static bool write_captured_report(const char *event, const char *detail)
{
	return write_captured_report_ex(false, event, detail);
}

// Latches once a clean-exit fatal has been logged, so a cascade (fread_die -> its caller ->
// JE_tyrianHalt, or dir_fopen_die -> JE_tyrianHalt) writes exactly one report for the one fault.
static volatile LONG s_cleanFatalLogged = 0;

// Public: log a "clean" fatal (exit()/_Exit paths that raise no exception) without terminating.
void crashlog_report_fatal(const char *event, const char *detail)
{
	if (InterlockedExchange(&s_cleanFatalLogged, 1) != 0)
		return;  // already reported this death; keep the first (most specific) report
	write_captured_report(event ? event : "FATAL (clean exit path)", detail);
}

// Public: log a recovered (non-fatal) problem. Does NOT latch s_cleanFatalLogged, so the game
// keeps running and any real crash later still writes its own report.
void crashlog_note(const char *event, const char *detail)
{
	write_captured_report(event ? event : "RECOVERED", detail);
}

// Public: same report, but into the net log, so netplay health events (desyncs, stalls,
// resyncs) can't bury a real crash report in the crash log.
void crashlog_note_net(const char *event, const char *detail)
{
	if (!crashlog_get_netlog_enabled())
		return;
	write_captured_report_ex(true, event ? event : "NETWORK", detail);
}

// Public: one short entry, no context/stack body -- the session start/end banners.
void crashlog_netlog_line(const char *event, const char *detail)
{
	if (!crashlog_get_netlog_enabled())
		return;

	if (InterlockedExchange(&s_reporting, 1) != 0)
		return;

	FILE *f = open_net_log();
	if (f != NULL)
	{
		time_t now = time(NULL);
		struct tm lt;
		char when[64] = "unknown";
		if (localtime_s(&lt, &now) == 0)
			strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &lt);

		fprintf(f, "--- %s --- %s (%s)\n", event, when,
		        (opentyrian_commit && *opentyrian_commit) ? opentyrian_commit : "?");
		if (detail != NULL)
			fprintf(f, "%s\n", detail);
		fputc('\n', f);
		fclose(f);
	}

	InterlockedExchange(&s_reporting, 0);
}

static void report_crt_fatal(const char *event, const char *detail)
{
	write_captured_report(event, detail);
	_exit(3);
}

static void on_abort(int sig)
{
	(void)sig;
	report_crt_fatal("ABORT (abort() / failed assert)", NULL);
}

static void __cdecl on_invalid_parameter(const wchar_t *expr, const wchar_t *func,
                                         const wchar_t *file, unsigned int line, uintptr_t unused)
{
	(void)unused;
	char detail[512];
	_snprintf_s(detail, sizeof(detail), _TRUNCATE,
	            "Invalid CRT parameter: %ls  (%ls, %ls:%u)",
	            expr ? expr : L"?", func ? func : L"?", file ? file : L"?", line);
	report_crt_fatal("CRT INVALID PARAMETER", detail);
}

static void __cdecl on_purecall(void)
{
	report_crt_fatal("PURE VIRTUAL CALL", NULL);
}

void install_crash_handler(void)
{
	// Must come first: every reporting path locks this around its dbghelp session, and this runs
	// before watchdog_init and before any handler is armed.
	InitializeCriticalSection(&s_dbghelpLock);

	// Remember who "main" is and when the session began (this runs on the main thread at startup).
	s_mainThreadId = GetCurrentThreadId();
	s_startTick    = GetTickCount64();

	// Name this session's logs before anything can want one. Still single-threaded here with no
	// handler installed, so the stamp is settled before any report could race for it.
	build_session_stamp();

	// Two catches so a real fault is hard to miss: the vectored handler (crash_veh) is primary, the
	// top-level filter a backup for whatever it doesn't take. SetUnhandledExceptionFilter alone can
	// be bypassed, which would leave a genuine fault unlogged.
	AddVectoredExceptionHandler(1, crash_veh);
	SetUnhandledExceptionFilter(crash_handler);

	// Broaden coverage to CRT-level fatals that raise no SEH exception.
	signal(SIGABRT, on_abort);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);  // no message box / WER popup
	_set_invalid_parameter_handler(on_invalid_parameter);
	_set_purecall_handler(on_purecall);
}

// Hang watchdog.
// A background thread logs the main thread's stack when the event heartbeat stalls.
// It resumes the thread and rearms after progress; it never terminates the process.

static volatile LONG s_heartbeat = 0;   // bumped by the main loop; frozen while it's stuck
static HANDLE        s_mainThread = NULL;
static volatile LONG s_hangLogged = 0;  // 1 once we've logged the current stall (avoid spamming)

void watchdog_heartbeat(void)
{
	InterlockedIncrement(&s_heartbeat);
}

static void watchdog_dump_hang(int seconds)
{
	FILE *f = open_log();
	if (f == NULL)
		return;

	write_header(f, "HANG (main thread stalled)");
	write_process_info(f, s_mainThreadId);
	fprintf(f, "Main thread made no progress for ~%d seconds -- likely an infinite loop.\n", seconds);
	fprintf(f, "Heartbeat: %ld\n\n", (long)s_heartbeat);

	// Capture registers under the briefest possible suspension, then resume before the stack walk:
	// symbolisation takes loader/CRT-heap locks, so walking while the main thread is frozen holding
	// one would deadlock. A hung thread makes no progress after resume, so its stack stays coherent.
	CONTEXT ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.ContextFlags = CONTEXT_FULL;

	if (SuspendThread(s_mainThread) == (DWORD)-1)
	{
		fprintf(f, "(could not suspend main thread)\n");
		fclose(f);
		return;
	}

	BOOL gotContext = GetThreadContext(s_mainThread, &ctx);
	ResumeThread(s_mainThread);  // resume immediately -- all remaining work runs unsuspended

	if (gotContext)
		write_context_report(f, s_mainThread, &ctx);
	else
		fprintf(f, "(could not read main-thread context)\n");

	fclose(f);
}

static DWORD WINAPI watchdog_proc(LPVOID unused)
{
	(void)unused;
	LONG last = s_heartbeat;
	int stalled = 0;

	for (;;)
	{
		Sleep(1000);

		const LONG hb = s_heartbeat;
		if (hb != last)
		{
			last = hb;
			stalled = 0;
			s_hangLogged = 0;  // progress resumed -> re-arm for the next stall
			continue;
		}

		// Threshold is read live each second (crashlog_get_hang_timeout), so lowering it in the
		// debug menu to catch a brief freeze takes effect within ~1s without a restart.
		if (++stalled >= crashlog_get_hang_timeout() && !s_hangLogged)
		{
			watchdog_dump_hang(stalled);
			s_hangLogged = 1;  // logged this stall; wait for progress (or manual kill)
		}
	}
}

void watchdog_init(void)
{
	// GetCurrentThread() is a pseudo-handle valid only in the calling thread; duplicate it into a
	// real handle the watchdog can suspend/inspect. Must be called on the main thread.
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
	                     GetCurrentProcess(), &s_mainThread,
	                     0, FALSE, DUPLICATE_SAME_ACCESS))
		return;

	HANDLE t = CreateThread(NULL, 0, watchdog_proc, NULL, 0, NULL);
	if (t != NULL)
		CloseHandle(t);  // fire-and-forget; the thread lives for the process lifetime
}

#else  // !_WIN32

// No crash handler or stack walker here, but netplay still needs its health log -- a desync
// against a console peer otherwise leaves no trace on that side. Reduced entries (header +
// detail only) go to log/opentyrian_net_<launch time>.log under the user directory: one file per
// session, written only if that session had something to report, and never overwritten by a
// later run. Same folder and naming as the Windows logs, minus the crash log there is no walker
// for.

#include "config.h"
#include "file.h"
#include "opentyr.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define LOG_DIR            "log"
#define LOG_PREFIX         "opentyrian_"  // what every log this game writes starts with
#define NETLOG_STEM        LOG_PREFIX "net"
#define NETLOG_NAME_MAX    48    // "<stem>_YYYY-MM-DD_HHMMSS.log" and room to spare
#define LEGACY_NETLOG_KEEP 3     // numbered generations older builds kept beside the live log

// Net-log master switch (Setup -> Network Log); see crashlog.h.
static bool s_netLogEnabled = true;

// Set once the net logs older builds left behind have been swept this run.
static bool s_legacyNetLogsSwept = false;

// This session's log name, fixed at startup so it reads as the launch time rather than whenever
// the first entry happened to land.
static char s_netLogName[NETLOG_NAME_MAX];

static const char *netlog_filename(void)
{
	if (s_netLogName[0] == '\0')
	{
		time_t now = time(NULL);
		const struct tm *lt = localtime(&now);
		char stamp[24] = "unknown";
		if (lt != NULL)
			strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H%M%S", lt);
		snprintf(s_netLogName, sizeof(s_netLogName), "%s_%s.log", NETLOG_STEM, stamp);
	}
	return s_netLogName;
}

// "<user dir>/log", without creating it -- the delete/scan paths must not conjure an empty folder.
static const char *log_dir(void)
{
	static char dir[560];

	if (dir[0] == '\0')
		snprintf(dir, sizeof(dir), "%s/%s", get_user_directory(), LOG_DIR);

	return dir;
}

// Same path, created on demand. Best-effort, exactly like the user directory's own mkdir in
// config.c: a genuine failure surfaces at the fopen that follows rather than here.
static const char *make_log_dir(void)
{
	const char *dir = log_dir();
	const int mkdir_result = mkdir(dir, 0700);  // already there -> EEXIST, harmless
	(void)mkdir_result;
	return dir;
}

// Remove the fixed-name and numbered logs older builds left behind, once per run: those were
// rewritten every launch by contract, so dropping them is what the build that wrote them would
// have done, and it stops a stale opentyrian_net.log posing as a current one. They predate the
// log folder, so they sit loose in the user directory.
static void sweep_legacy_net_logs(void)
{
	if (s_legacyNetLogsSwept)
		return;
	s_legacyNetLogsSwept = true;

	const char *dir = get_user_directory();
	char path[600];

	snprintf(path, sizeof(path), "%s/%s.log", dir, NETLOG_STEM);
	remove(path);  // nothing there -> fails harmlessly

	for (int n = 1; n <= LEGACY_NETLOG_KEEP; ++n)
	{
		snprintf(path, sizeof(path), "%s/%s.%d.log", dir, NETLOG_STEM, n);
		remove(path);
	}
}

// This session's log names itself, so there is nothing to reserve; all this does is drop the
// leftovers of older builds. Off means untouched.
void crashlog_netlog_begin_session(void)
{
	if (!s_netLogEnabled)
		return;

	sweep_legacy_net_logs();
}

static void netlog_write(const char *event, const char *detail)
{
	if (!s_netLogEnabled)
		return;

	// Always appends: the name carries the launch time, so there is never a stale file to clear,
	// and a clear mid-session just starts this one over (folder included).
	FILE *f = dir_fopen(make_log_dir(), netlog_filename(), "a");
	if (f == NULL)
		return;

	time_t now = time(NULL);
	const struct tm *lt = localtime(&now);
	char when[64] = "unknown";
	if (lt != NULL)
		strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", lt);

	fprintf(f, "--- %s --- %s (%s)\n", event, when,
	        (opentyrian_commit && *opentyrian_commit) ? opentyrian_commit : "?");
	if (detail != NULL)
		fprintf(f, "%s\n", detail);
	fputc('\n', f);
	fclose(f);
}

// Nothing to install, but this is the main thread at startup: pin the session stamp here so the
// log is named for when the session began.
void install_crash_handler(void) { (void)netlog_filename(); }

void watchdog_init(void) { }
void watchdog_heartbeat(void) { }
void crashlog_report_fatal(const char *event, const char *detail) { (void)event; (void)detail; }
void crashlog_note(const char *event, const char *detail) { (void)event; (void)detail; }

void crashlog_note_net(const char *event, const char *detail)
{
	netlog_write(event ? event : "NETWORK", detail);
}

void crashlog_netlog_line(const char *event, const char *detail)
{
	netlog_write(event, detail);
}

void crashlog_set_netlog_enabled(bool enabled) { s_netLogEnabled = enabled; }
bool crashlog_get_netlog_enabled(void) { return s_netLogEnabled; }

// Delete every "opentyrian_*.log" in `dir`, returning how many went. Matching on the game's own
// filename prefix rather than a bare *.log keeps this safe to point at the user directory, which
// holds the saves and config as well; it takes in the timestamped logs, the fixed and numbered
// names older builds wrote, and the crash logs the desktop build names the same way -- everything
// this game logs, not just the net side of it.
static int delete_logs_in(const char *dir)
{
	DIR *d = opendir(dir);
	if (d == NULL)
		return 0;

	int deleted = 0;
	const struct dirent *e;
	while ((e = readdir(d)) != NULL)
	{
		const size_t len = strlen(e->d_name);
		if (len < sizeof(LOG_PREFIX) - 1 + 4)
			continue;
		if (strncmp(e->d_name, LOG_PREFIX, sizeof(LOG_PREFIX) - 1) != 0)
			continue;
		if (strcmp(e->d_name + len - 4, ".log") != 0)
			continue;

		char path[600];
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		if (remove(path) == 0)
			++deleted;
	}
	closedir(d);

	return deleted;
}

// Delete every log the system holds, this session's included -- there is no file manager on a
// console to prune them with. The log folder plus the user directory itself, for the loose ones
// older builds wrote. Returns true if at least one went, so the menu can tell "cleared" from
// "there was nothing there".
bool crashlog_clear_logs(void)
{
	const int deleted = delete_logs_in(log_dir()) + delete_logs_in(get_user_directory());

	return deleted > 0;
}

#endif
