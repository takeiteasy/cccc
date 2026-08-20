// Ticket #1085: a va_list forwarded to a host libc v*-family function
// (vprintf/vsnprintf/vfprintf/vsscanf/vsyslog/...) used to silently
// disagree between the VM and native, on every host -- the mirror bug to
// #1062 (a va_list forwarded as an ordinary function *parameter*), one
// layer further out.
//
// #1062's own filing text claimed the VM's own struct va_list was always
// genuinely by-value and only glibc's real (array-decayed) va_list
// aliased. That premise turned out backwards for THIS ticket specifically,
// confirmed directly before building anything on it (this batch's own
// recurring lesson -- #1046/#1044/#1042(c)/#1062/#1078/#1082 all had wrong
// or stale stated causes too): a struct/union by-value argument reaches
// an FFI cfunc by the CALLER's own address (#714), and unlike a
// CCCC-emitted callee (which gets a genuine prologue copy per #1078), the
// v*-family FFI wrappers (src/stdlib/format_printf.c, format_scanf.c,
// stdio.c, posix_lang.c) had no such copy -- they extracted args straight
// out of the caller's own va_list struct, silently advancing it. This was
// true on the VM on EVERY host (macOS included -- it has nothing to do
// with glibc's array-decay quirk, that's #1062's own residual, not this
// one). Fixed on the VM side by CCCC_VA_LOCAL (src/stdlib/va_ffi_helper.h),
// a snapshot-before-extract macro used at all twelve wrapper sites.
//
// Separately, -c=native's own real glibc va_list (an array type, decays
// to a pointer in parameter position, C17 6.7.6.3p7) genuinely does alias
// the caller's own storage when passed to any function taking a va_list
// parameter, including the host's own v*-family functions -- this half
// IS the shape #1062's filing predicted, just for a callee CCCC never
// emits a prologue for. Fixed by wrapping any call passing a va_list-typed
// argument in a va_copy'd statement expression at the CALL SITE
// (serialize_expr's ND_FUNCALL case, src/serialize.c) rather than only at
// a CCCC-emitted callee's own prologue -- this covers host libc calls,
// indirect calls through a function pointer, and (redundantly but
// harmlessly, since a second copy of an already-independent copy changes
// nothing observable) calls to a CCCC-emitted callee too.
//
// IMPORTANT test-design note: every case here calls the v*-family function
// DIRECTLY in the same function that owns `ap` (no intervening function
// call taking a va_list parameter). #1078 already made every struct/union
// by-value parameter genuinely copy-on-call on the VM, so passing `ap`
// through an ordinary intermediate function would already isolate it from
// the original regardless of Part A's own fix -- that would test #1078,
// not #1085. The real #1085 VM bug only shows up when the v*-family
// function itself is called while `ap` is still needed afterward, in the
// very frame that owns it.
//
// Confirmed failing pre-fix on the VM on macOS (no glibc/container needed
// to observe the VM half of this bug, unlike #1062's own residual, which
// needs real glibc for its native half).

#include "stdio.h"
#include "stdarg.h"
#include "string.h"

// Basic case: vsnprintf must not consume the caller's own va_list.
static int test_vsnprintf_basic(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int first = va_arg(ap, int);        // consumes 10, ap now positioned at 20
    char buf[32];
    vsnprintf(buf, sizeof buf, "%d", ap); // must format "20" without
                                           // advancing ap itself
    int second = va_arg(ap, int);       // ap must still read 20 -- the same
                                         // value vsnprintf itself just read,
                                         // not something past it
    va_end(ap);
    if (first != 10) return 1;
    if (strcmp(buf, "20") != 0) return 2;
    if (second != 20) return 3;
    return 0;
}

// The canonical, ubiquitous-in-real-code "measure then format" idiom: one
// va_list forwarded to vsnprintf(NULL, 0, ...) to get the required length,
// then forwarded a second time (same va_list, no re-va_start) to actually
// format -- both calls must see the SAME arguments. Pre-fix, the first
// call silently advances ap (VM: in-place extraction; native/glibc:
// array-decay aliasing), so the second call reads whatever comes AFTER the
// real arguments instead -- `needed` and `written` disagree, or the
// formatted text is wrong. This is exactly "a real program relies on it,"
// the case #1085 was filed to wait for.
static int measure_then_format(char *buf, int bufsz, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(0, 0, fmt, ap);
    int written = vsnprintf(buf, (unsigned long)bufsz, fmt, ap);
    va_end(ap);
    if (needed != written) return -1;
    return written;
}

static int test_measure_then_format(void) {
    char buf[64];
    int n = measure_then_format(buf, sizeof buf, "%d-%s-%d", 7, "mid", 99);
    if (n < 0) return 1;
    if (strcmp(buf, "7-mid-99") != 0) return 2;
    return 0;
}

// vfprintf to a real stream (not stdout, so the test stays quiet): must not
// consume the caller's own va_list either.
static int test_vfprintf_forward(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int first = va_arg(ap, int); // consumes 10, ap now positioned at 20
    FILE *f = fopen("/dev/null", "w");
    if (!f) return 1;
    vfprintf(f, "%d", ap); // must write 20 without advancing ap
    fclose(f);
    int second = va_arg(ap, int); // ap must still read 20
    va_end(ap);
    if (first != 10) return 2;
    if (second != 20) return 3;
    return 0;
}

// vsscanf forwarding: the scanf family's own v*-wrapper (format_scanf.c)
// gets the same treatment. vsscanf must write through the pointer argument
// without consuming it from the caller's own va_list.
static int test_vsscanf_forward(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int got = vsscanf("42", "%d", ap);   // writes through the pointer arg
    int *reread = va_arg(ap, int *);     // ap must be UNCHANGED -- still
                                          // positioned at the same
                                          // (unconsumed) pointer argument
    va_end(ap);
    if (got != 1) return 1;
    if (!reread) return 2;
    if (*reread != 42) return 3; // confirms vsscanf really did write
                                  // through this pointer
    return 0;
}

// Control: a va_list that never reaches any v*-family function at all must
// obviously be unaffected by this fix -- a control against a bug that
// miscounts even when no v*-family call is ever made.
static int test_untouched_control(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int first = va_arg(ap, int);
    int second = va_arg(ap, int);
    va_end(ap);
    if (first != 10) return 1;
    if (second != 20) return 2;
    return 0;
}

int main(void) {
    int r;
    int scanf_target = -1;
    if ((r = test_vsnprintf_basic(2, 10, 20)) != 0) return r;
    if ((r = test_measure_then_format()) != 0) return 10 + r;
    if ((r = test_vfprintf_forward(2, 10, 20)) != 0) return 20 + r;
    if ((r = test_vsscanf_forward(1, &scanf_target)) != 0) return 30 + r;
    if ((r = test_untouched_control(2, 10, 20)) != 0) return 40 + r;
    return 42;
}
