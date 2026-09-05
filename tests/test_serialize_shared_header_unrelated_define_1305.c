// #1305 (follow-up to #1304): push_emit_directive()'s (src/preprocess.c)
// #1304 dedup-relocation fix scanned for a #define/#undef captured
// ANYWHERE between the two occurrences of an identical #include line, not
// just ones the SECOND occurrence's own TU captured. A totally unrelated
// #define -- captured by the FIRST (kept) TU for a completely different
// header, with no bearing on the shared one at all -- wrongly triggered
// the same relocation, moving the shared header's replay position past
// every use the first TU had already made of it.
//
// Found via #1132's self-hosting spike round 14: src/stdlib/format.h (a
// plain, non-IMPLEMENTATION-style shared header, exactly this shape) is
// #include'd by both format_printf.c and format_scanf.c. format_printf.c
// (processed first) uses format.h's CCCC_DECFMT_* macros inside
// stb_sprintf.h's own captured/replayed body -- but format_printf.c ALSO
// defines STB_SPRINTF_IMPLEMENTATION (to configure stb_sprintf.h, an
// entirely different header) between format.h's #include and
// format_scanf.c's later, identical #include of format.h. The old scan
// found that unrelated #define and relocated format.h's #include all the
// way to format_scanf.c's position -- past format_printf.c's own need for
// it -- producing "use of undeclared identifier 'CCCC_DECFMT_MINUS'" under
// -c=native.
//
// This file (TU A, command-line input 0) reproduces the same shape: it
// captures plain_config_1305.h first, then defines
// VENDORED_1305_IMPLEMENTATION (for the unrelated vendored_1305_lib.h) in
// between, then uses plain_config_1305.h's own CFG_1305_VAL inside
// vendored_1305_lib.h's captured body. fixtures/
// shared_header_unrelated_define_1305_b.c (TU B) re-includes
// plain_config_1305.h with identical spelling, triggering the dedup.
//
// Fix: push_emit_directive()'s relocation scan is now scoped to directives
// captured by the CURRENT (deduping) TU only (emit_directives_tu_start,
// set once per TU in main.c's per-TU loop) -- only a #define/#undef the
// SECOND occurrence's own TU captured ahead of its own #include can mean
// "this TU configured the header before including it"; an earlier TU's
// unrelated capture never qualifies. #1304's own scenario (the configuring
// #define IS within the second TU's own range) still relocates correctly --
// see tests/test_serialize_vendored_multi_tu_include_1304.c, re-verified
// green by this fix.
//
// tools/comptime_native_smoke.py's case_shared_header_unrelated_define_1305
// is the load-bearing VM-42-to-native-42 proof -- a plain -m shape
// assertion alone doesn't invoke the host compiler and can't see the
// "undeclared identifier" this fix prevents.
// CCCC_FLAGS: tests/fixtures/shared_header_unrelated_define_1305_b.c
#include "fixtures/plain_config_1305.h"
#define VENDORED_1305_IMPLEMENTATION
#include "fixtures/vendored_1305_lib.h"

int shared_header_1305_call_a(void) {
    return v1305_use_cfg(3);
}
