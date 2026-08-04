/* host_backtrace.c -- Symbolic host C stack traces on crash (ticket #553).
 *
 * When CCCC_HAS_BACKTRACE is defined (default: on), a top-level signal handler
 * is installed at startup that prints a symbolic host C backtrace to stderr
 * before re-raising the original fault signal.  This catches crashes during
 * *any* CCCC phase (parse, codegen, VM dispatch) — not just inside vm_eval.
 *
 * The handler uses libbacktrace (vendored in src/backtrace/) with the mmap
 * allocator so it never calls malloc and is therefore safe to invoke from a
 * signal handler after the initial warm-up call.
 *
 * On Linux the -g build embeds DWARF inline → full file:line out of the box.
 * On macOS the binary carries only a debug map; run
 * `./cccc --build build.c --build-target=dsym` first to produce cccc.dSYM
 * and unlock file:line resolution. Without a dSYM, libbacktrace reports "no
 * debug info" during its warm-up pass on every run -- expected, and kept
 * silent (see bt_init_error_cb below) rather than printed on every
 * successful, non-crashing invocation.
 */

#include "internal.h"

#ifdef CCCC_HAS_BACKTRACE

#if !defined(_WIN32) && !defined(_WIN64)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "backtrace.h"

/* Backtrace state — created once in cc_host_backtrace_init(). */
static struct backtrace_state *bt_state;

/* ---------- libbacktrace callbacks ---------- */

static void bt_error_cb(void *data, const char *msg, int errnum) {
    (void)data;
    if (errnum > 0)
        fprintf(stderr, "  [backtrace error: %s (%d)]\n", msg, errnum);
    else
        fprintf(stderr, "  [backtrace error: %s]\n", msg);
}

/* Silent variant used only for state creation and the warm-up pass (see
 * cc_host_backtrace_init below): on macOS without a .dSYM, libbacktrace
 * reports "no debug info in Mach-O executable" during warm-up on every
 * single run, even fully successful ones with no crash -- that's normal,
 * expected, and not something a user running `cccc foo.c` should ever see
 * on stderr. A missing-debug-info message is only actually useful when it
 * happens at cc_host_backtrace_print() time, i.e. while resolving a real
 * crash, so bt_error_cb above stays wired up there. */
static void bt_init_error_cb(void *data, const char *msg, int errnum) {
    (void)data; (void)msg; (void)errnum;
}

typedef struct {
    int frame;
} BtPrintData;

static int bt_full_cb(void *data, uintptr_t pc,
                      const char *filename, int lineno,
                      const char *function) {
    BtPrintData *d = (BtPrintData *)data;
    if (function)
        fprintf(stderr, "  #%-2d  %s", d->frame, function);
    else
        fprintf(stderr, "  #%-2d  <%p>", d->frame, (void *)pc);
    if (filename) {
        /* Strip leading path up to the repo root so paths stay readable. */
        const char *src = strstr(filename, "/src/");
        if (!src) src = filename;
        fprintf(stderr, " (%s:%d)", src, lineno);
    }
    fprintf(stderr, "\n");
    d->frame++;
    return 0;
}

/* ---------- Public API ---------- */

void cc_host_backtrace_print(void) {
    if (!bt_state)
        return;
    BtPrintData d = { 0 };
    /* skip=2: skip cc_host_backtrace_print itself and the signal trampoline */
    backtrace_full(bt_state, 2, bt_full_cb, bt_error_cb, &d);
}

/* Top-level crash handler: print trace, then re-raise with default disposition
 * so the process terminates with the original signal (exit code preserved for
 * the test runner). SIGABRT is excluded so assert/abort in negative tests
 * behave normally. */
static const int fatal_signals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL };
#define FATAL_SIGNAL_COUNT ((int)(sizeof(fatal_signals)/sizeof(fatal_signals[0])))

static void host_crash_handler(int sig) {
    /* Restore default disposition immediately so a recursive fault terminates
     * the process normally rather than looping here. */
    signal(sig, SIG_DFL);

    const char *name = "signal";
    if      (sig == SIGSEGV) name = "SIGSEGV";
    else if (sig == SIGBUS)  name = "SIGBUS";
    else if (sig == SIGFPE)  name = "SIGFPE";
    else if (sig == SIGILL)  name = "SIGILL";

    fprintf(stderr, "\nHost C crash (%s):\n", name);
    fflush(stderr);
    cc_host_backtrace_print();
    fflush(stderr);

    /* Re-raise — the default disposition is now active so the process dies
     * with the original signal. */
    raise(sig);
}

/* Silent callback used only for the warm-up pass. */
static int bt_warmup_cb(void *data, uintptr_t pc,
                        const char *filename, int lineno,
                        const char *function) {
    (void)data; (void)pc; (void)filename; (void)lineno; (void)function;
    return 0; /* keep going, discard all output */
}

void cc_host_backtrace_init(const char *argv0) {
    /* threaded=1: use atomic ops for thread safety. bt_init_error_cb: see
     * its comment above -- state creation/warm-up failures (e.g. no .dSYM
     * on macOS) are expected and must not print on a successful run. */
    bt_state = backtrace_create_state(argv0, 1, bt_init_error_cb, NULL);

    /* Warm-up: perform one full backtrace now so that libbacktrace loads and
     * mmaps all DWARF/Mach-O data before any signal occurs.  After this call
     * the handler path never calls malloc or opens files. */
    if (bt_state)
        backtrace_full(bt_state, 0, bt_warmup_cb, bt_init_error_cb, NULL);
}

void cc_host_backtrace_install_fatal(void) {
    if (!bt_state)
        return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = host_crash_handler;
    /* SA_RESETHAND: auto-restore default after first delivery so recursive
     * faults are not caught again. */
    sa.sa_flags = SA_RESETHAND;
    for (int i = 0; i < FATAL_SIGNAL_COUNT; i++)
        sigaction(fatal_signals[i], &sa, NULL);
}

#else /* Windows — not yet supported */

void cc_host_backtrace_init(const char *argv0)   { (void)argv0; }
void cc_host_backtrace_install_fatal(void)         {}
void cc_host_backtrace_print(void)                 {}

#endif /* !Windows */

#else /* CCCC_HAS_BACKTRACE not defined */

void cc_host_backtrace_init(const char *argv0)   { (void)argv0; }
void cc_host_backtrace_install_fatal(void)         {}
void cc_host_backtrace_print(void)                 {}

#endif /* CCCC_HAS_BACKTRACE */
