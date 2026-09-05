// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: #include "fixtures/setjmp_transitive_1299\.h"
// CCCC_REJECT_STDOUT: extern int _setjmp
// CCCC_REJECT_STDOUT: extern void _longjmp
//
// #1299: found via #1132's round-13 self-hosting spike (src/cccc.h's own
// `#include <setjmp.h>`, reached the same way this fixture reaches it).
// serialize_synth_setjmp_decls() (src/serialize_program.c) always emitted
// its own void*-shaped `extern int _setjmp(void *)`/`extern void
// _longjmp(void *, int)` declarations for a TU using setjmp/longjmp --
// correct when <setjmp.h> is never otherwise reachable (the ordinary,
// suppressed, top-level case #1054/#1030 already covers), but a captured,
// ordinary project header that itself contains `#include <setjmp.h>`
// (this test's own fixture) has its own inclusion replayed as ONE line,
// and the host re-reads its real, unmodified content straight off disk,
// following its nested #include <setjmp.h> with no chance for the
// top-level suppression to intervene -- so the real host setjmp.h AND
// these synth declarations both reached the same output, a host
// "conflicting types for '_setjmp'" error. This test only checks -m's
// shape (the synth declarations must not appear when setjmp.h is
// reachable this way); tools/comptime_native_smoke.py's case is what
// proves the resulting -c=native output actually links.
#include "fixtures/setjmp_transitive_1299.h"

static jmp_buf env;

static void unwind(void) {
    longjmp(env, 42);
}

int main(void) {
    int rv = setjmp(env);
    if (rv == 0) {
        unwind();
        return 1; // unreachable
    }
    return rv;
}
