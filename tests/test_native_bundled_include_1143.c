// CCCC_FLAGS: --posix-emulation
//
// #1143: -c=native's run_native_backend() forwards every user -I verbatim
// to the host compiler -- so tools/testing/native.py's own `-I./include`
// (this repo's own test harness's standard invocation, needed for a quoted
// `#include "local.h"` in some OTHER test to resolve) used to shadow the
// real host headers with CCCC's own bundled, guest-side copies whenever a
// TU's replayed #include line happened to name one CCCC also bundles.
// Confirmed with a single TU combining <sched.h>/<locale.h> (CCCC's own
// bundled copies) with <pthread.h> (which has a real #include_next hand-off
// to the host since #1022, transitively reaching the host's own,
// differently-shaped struct sched_param/struct lconv/locale_t/freelocale):
// "redefinition of sched_param", "redefinition of lconv", "typedef
// redefinition ... locale_t", "conflicting types for freelocale" -- four
// of ten errors this exact combination produced pre-fix (the other six,
// from <netdb.h>/<signal.h>/<netinet/in.h>, are exercised by
// test_posix_native_shims_1140.c and test_posix_native_canonical_1146.c,
// already un-skipped by this same fix).
//
// Fixed by demoting exactly the -I/-isystem entries that actually resolved
// one of CCCC's own bundled headers (cc_include_dir_is_cccc_bundled(),
// preprocess.c) to `-idirafter` instead of `-I`/`-isystem` when forwarding
// to the host compiler (run_native_backend(), main.c) -- the real host
// header always wins the search now, while a header the host genuinely
// lacks still resolves as a last resort. include/sched.h and
// include/locale.h themselves are untouched -- with the bundled dir
// demoted, the host compiler never reads them at all under -c=native, so
// no #include_next hand-off (the fix this ticket originally proposed) was
// needed there.
#include <locale.h>
#include <pthread.h>
#include <sched.h>
#include <math.h>
#include <unistd.h>

// #1143 regression, found while adding native-corpus coverage for #1129/
// #1130: this fix's own -idirafter demotion swept in two headers that were
// never meant to hand off at all -- math.h/float.h (zero #include_next in
// either, documented in man/HEADERS.md as complete, self-contained
// polyfills) and unistd.h (also zero #include_next; its own bundled copy
// declares mkstemp as a same-directory convenience, matching macOS/BSD,
// but real glibc puts mkstemp in <stdlib.h> instead). Both regressed to
// "undeclared identifier" under -c=native with -I./include once demoted --
// re-fixed by forcing CCCC's own math.h/float.h via an absolute-path
// #include substitution (find_cccc_bundled_header_path(), preprocess.c)
// and adding a fourth companion-include entry (unistd.h -> stdlib.h,
// serialize_program.c) alongside #1143's existing three.
// setpayload/getpayload: the C23 IEEE family real macOS libm lacks
// entirely (#1037, permanent -- calling them would fail at *link* time
// there, a different, already-documented gap this test isn't about) and
// real glibc only declares behind feature-test macros CCCC's own bundled
// math.h doesn't gate on -- exactly the family this regression made
// "undeclared identifier" (a *compile*-time failure) rather than #1037's
// link-time one. Linux-only check; macOS's own gap is covered separately
// by tests/test_setpayload_zero_1079.c (NATIVE_SKIP_TESTS_MACOS).
#ifdef __linux__
static int check_c23_ieee_math(void) {
    double nan_val = NAN;
    if (setpayload(&nan_val, 42.0) != 0)
        return 1;
    if (getpayload(&nan_val) != 42.0)
        return 2;
    return 0;
}
#endif

static int check_mkstemp(void) {
    char tmpl[] = "/tmp/cccc_1143_XXXXXX";
    int  fd     = mkstemp(tmpl);
    if (fd < 0)
        return 1;
    close(fd);
    unlink(tmpl);
    return 0;
}

int main(void) {
#ifdef __linux__
    if (check_c23_ieee_math())
        return 7;
#endif
    if (check_mkstemp())
        return 8;
    // struct sched_param -- CCCC's own bundled 4-byte guest-visible copy
    // vs. the host's own (differently-sized on macOS) real one, reached
    // transitively via <pthread.h>'s #include_next hand-off.
    struct sched_param sp = {0};
    if (sched_get_priority_min(SCHED_OTHER) < 0)
        return 1;
    (void)sp;

    // struct lconv / locale_t / freelocale -- CCCC's own bundled copies vs.
    // the host's own real ones.
    struct lconv *lc = localeconv();
    if (!lc)
        return 2;
    locale_t loc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    if (!loc)
        return 3;
    freelocale(loc);

    // pthread.h itself, to actually exercise the real host hand-off this
    // whole collision depends on. pthread_mutex_init() rather than the
    // PTHREAD_MUTEX_INITIALIZER macro -- a stack-local mutex initialized
    // with that macro hits an unrelated, pre-existing gap (its lowering
    // path differs from a global's, and isn't covered by #1022's own
    // designated-init special case), out of this ticket's own scope.
    pthread_mutex_t mtx;
    if (pthread_mutex_init(&mtx, NULL) != 0)
        return 4;
    if (pthread_mutex_lock(&mtx) != 0)
        return 5;
    if (pthread_mutex_unlock(&mtx) != 0)
        return 6;
    pthread_mutex_destroy(&mtx);

    return 42;
}
