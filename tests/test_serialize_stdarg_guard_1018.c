// Ticket #1018 follow-up 2: found while verifying #1018's va_start/va_arg/
// va_copy/va_end translation on real Linux (cccc-linux-amd64 container,
// clang 18) -- every prior verification pass in this batch had been
// macOS-only. include/stdarg.h's own outer include guard used to wrap the
// ENTIRE file, including the `#else #include_next <stdarg.h> #endif`
// hand-off to the real host header. glibc's real <stdio.h> issues its own
// PARTIAL stdarg.h request first (`#define __need___va_list` then
// `#include <stdarg.h>`, glibc's standard idiom for wanting only
// __gnuc_va_list, not the full va_start/va_arg machinery) to pick up
// __gnuc_va_list for its own prototypes -- clang's real <stdarg.h> handles
// that correctly (it does NOT set its own guard on a partial pass, so a
// later full request still runs). But CCCC's OWN outer guard was already
// permanently set by that first partial pass, so the LATER, full
// `#include <stdarg.h>` (this file's own, or any TU-level one) hit
// `#ifndef <guard>` already false and skipped the #include_next entirely
// -- va_start/va_arg/va_end never got macro-defined at all ("call to
// undeclared library function 'va_start'").
//
// Fixed by moving the guard to wrap only the `#ifdef __CCCC__` branch's
// own body (CCCC's own struct/macros only need defining once per TU,
// ordinary header-guard reasoning) and leaving the `#else` branch's
// `#include_next <stdarg.h>` unconditional -- every #include <stdarg.h>,
// partial or full, now always reaches the real host header, which already
// has its own correct guard/partial-request logic.
//
// This file doesn't reproduce the glibc-specific partial-request idiom
// directly (that's Linux-only, glibc-internal, and this batch's own repro
// lives in the ticket comment) -- it instead exercises the more general
// shape the guard fix protects: a real <stdio.h> include (which pulls in
// -- and, on Linux, has historically pulled in via -- some form of
// va_list machinery) followed by this program's own explicit
// <stdarg.h>-using variadic function. Confirmed against the real
// container that the pre-fix binary fails to compile this exact shape
// natively ("call to undeclared library function 'va_start'"), and the
// post-fix binary round-trips VM 42 -> native 42 there.

#include "stdio.h"
#include "stdarg.h"

static int sum_ints(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

int main() {
    FILE *f = 0;
    (void)f;
    if (sum_ints(3, 10, 12, 20) != 42)
        return 1;
    return 42;
}
