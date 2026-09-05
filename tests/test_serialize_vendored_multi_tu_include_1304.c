// #1304 (follow-up to #1301): -c=native/-m's #include dedup can pick the
// non-IMPLEMENTATION TU's line for a vendored single-header library
// #include'd identically by two TUs -- one of them, but not necessarily the
// first one captured, defines the IMPLEMENTATION-style macro ahead of its
// own #include. push_emit_directive() (src/preprocess.c) deduped an
// identical #include line text first-wins, permanently -- so if the
// non-defining TU's own #include was captured first (this file is command-
// line input 0, and never defines VENDORED_1304_IMPLEMENTATION), its
// position won and fixtures/vendored_1304_b.c's own `#define
// VENDORED_1304_IMPLEMENTATION` -- captured afterwards -- was replayed too
// late to configure the header by the time it reached the host compiler.
// v1304_add()'s body was silently dropped from -c=native/-m output (an
// "undefined symbol" link error, confirmed pre-fix; #1301's own
// function_is_header_supplied() suppression assumes the replayed #include
// supplies the body, which is exactly what made this a *silent* loss
// rather than a redefinition error).
//
// Fix: an identical-text dedup hit is relocated to the position of the
// LAST TU's own #include, but only when a #define/#undef was captured in
// between -- see push_emit_directive's own comment. This file's own #include
// (never defining the macro) is deliberately listed first on the command
// line so the dedup hit lands here, exercising the relocation directly.
// tools/comptime_native_smoke.py's case_vendored_multi_tu_include_1304 is
// the load-bearing VM-42-to-native-42 proof -- this file's own -m shape
// assertion can't see a link failure any more than #1301's own test could.
// CCCC_FLAGS: tests/fixtures/vendored_1304_b.c
#include "fixtures/vendored_1304_lib.h"

int vendored_1304_call_a(void) {
    return v1304_add(20);
}
