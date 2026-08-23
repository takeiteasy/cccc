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

int main(void) {
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
