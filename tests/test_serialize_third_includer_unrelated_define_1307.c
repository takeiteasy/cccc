// #1307 (follow-up to #1305/#1306): scoping the dedup-relocation scan to
// the CURRENT occurrence's own TU (#1305's own fix in
// push_emit_directive(), src/preprocess.c, and #1306's mirror in
// cc_serialize_program()'s canonical-path dedup, src/serialize_program.c)
// still wasn't precise enough -- ANY #define/#undef that TU happens to
// capture anywhere ahead of its own #include, not just one that actually
// configures the header in question, still won.
//
// Found via #1132's self-hosting spike round 14, immediately after #1305
// landed: src/stdlib/format.h is reached identically by THREE TUs --
// format_printf.c (needs it before stb_sprintf.h's own captured body),
// format_scanf.c (plain second includer), and src/stdlib/stdio.c (a
// THIRD includer with its own, wholly unrelated `#define
// CCCC_HAVE_NATIVE_PCT_B 1` guarding an unrelated glibc feature probe,
// captured earlier in the same file). #1305's own TU-scoped scan still
// found that unrelated #define anywhere in stdio.c's own captured range
// and relocated format.h's #include to stdio.c's position -- later than
// format_printf.c's own need for it, reproducing the exact "use of
// undeclared identifier 'CCCC_DECFMT_MINUS'" #1305 was meant to fix.
//
// This file (TU A, command-line input 0) reproduces the three-includer
// shape: fixtures/third_includer_unrelated_define_1307_b.c (TU B) is a
// plain second includer; fixtures/
// third_includer_unrelated_define_1307_c.c (TU C) mirrors stdio.c
// exactly -- its own unrelated #define lives inside a conditional-group
// shell (#1064), so the directive CAPTURED IMMEDIATELY before its own
// #include is the shell's `#endif`, not the #define itself.
//
// Fix: both #1305's and #1306's scans are narrowed from "any #define/
// #undef this TU captured ahead of its own #include" to "the directive
// captured IMMEDIATELY before this occurrence, in the same TU" -- the
// only pattern a real IMPLEMENTATION-style header actually relies on. A
// conditional-group shell is captured too (#1064) but is never itself a
// #define/#undef, so TU C's own `#endif` correctly fails the check and
// plain_config_1307.h stays at the test file's own (first) position.
// #1304's own scenario (the configuring #define genuinely IS the
// directive immediately ahead of the second occurrence) and #1305/#1306's
// own scenarios all still relocate/don't-relocate correctly under this
// narrower check -- their own tests are re-verified green by this fix.
//
// tools/comptime_native_smoke.py's
// case_third_includer_unrelated_define_1307 is the load-bearing
// VM-42-to-native-42 proof -- a plain -m shape assertion alone doesn't
// invoke the host compiler and can't see the "undeclared identifier"
// this fixes.
// CCCC_FLAGS: tests/fixtures/third_includer_unrelated_define_1307_b.c tests/fixtures/third_includer_unrelated_define_1307_c.c
#include "fixtures/plain_config_1307.h"
#define VENDORED_1307_IMPLEMENTATION
#include "fixtures/vendored_1307_lib.h"

int third_includer_1307_call_a(void) {
    return v1307_use_cfg(3);
}
