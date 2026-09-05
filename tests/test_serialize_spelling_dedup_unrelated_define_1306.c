// #1306 (sibling to #1305): cc_serialize_program()'s (src/serialize_program.c)
// own #1292 canonical-path dedup -- which collapses TWO DIFFERENT literal
// spellings of one on-disk header to a single replayed #include -- had the
// identical flaw #1305 fixed in push_emit_directive()'s sibling (identical-
// text) dedup, but worse: it tracked a single `last_define_idx` watermark
// across the WHOLE captured-directive array, so at any real multi-TU scale
// (dozens of files, thousands of intervening #define/#undef for entirely
// unrelated headers) the NEXT occurrence of a repeated canonical key
// almost always looked "newer" than the running candidate and won,
// regardless of any actual configuring relationship.
//
// Found via #1132's self-hosting spike round 14, right after #1305 landed:
// src/internal.h is reached under three different spellings --
// "internal.h" (src/arena.c and others), "./internal.h" (most of
// src/*.c, including src/macros.c and src/reflection.c, both of which
// need internal.h's Node/Type/Obj/Token typedefs before their own
// #include of reflection_ffi_protos.inc, a captured-and-replayed .inc
// carrying real code), and "../internal.h" (every src/stdlib/*.c file,
// processed only after ALL of src/*.c). The old dedup always preferred
// the LAST spelling encountered -- the stdlib one -- moving internal.h's
// replayed #include to AFTER reflection_ffi_protos.inc's own need for it:
// "unknown type name 'Node'"/'Type'/'Obj'/'Token' under -c=native (253 of
// this round's 263 real errors, once clang's default 20-error cap was
// lifted to see the full picture).
//
// This file (TU A, command-line input 0) reproduces the same shape: it
// captures fixtures/config_1306.h (spelling "fixtures/config_1306.h",
// relative to tests/), then defines VENDORED_1306_IMPLEMENTATION (for the
// unrelated vendored_1306_lib.h) in between, then uses config_1306.h's
// CFG_1306_VAL inside vendored_1306_lib.h's captured body. fixtures/
// spelling_dedup_unrelated_define_1306_b.c (TU B) re-includes
// config_1306.h under the DIFFERENT spelling "config_1306.h" (relative to
// its own directory, tests/fixtures/) -- both resolve to the same file,
// triggering cc_serialize_program()'s canonical-path dedup rather than
// push_emit_directive()'s identical-text one.
//
// Fix: the candidate-index forward pass now resets its own
// `local_last_define` watermark at every TU boundary
// (vm->compiler.emit_directives_tu_starts, shared with #1305's fix) --
// only a #define/#undef the CURRENT occurrence's own TU captured ahead of
// it can win, mirroring push_emit_directive()'s #1305 scoping exactly.
// #1304's own spelling-variant scenario (the configuring #define IS within
// the second spelling's own TU) still relocates correctly -- see
// tests/test_serialize_vendored_multi_tu_include_1304.c's spelling variant,
// re-verified green by this fix.
//
// tools/comptime_native_smoke.py's
// case_spelling_dedup_unrelated_define_1306 is the load-bearing
// VM-42-to-native-42 proof -- a plain -m shape assertion alone doesn't
// invoke the host compiler and can't see the "unknown type name" this
// fixes.
// CCCC_FLAGS: tests/fixtures/spelling_dedup_unrelated_define_1306_b.c
#include "fixtures/config_1306.h"
#define VENDORED_1306_IMPLEMENTATION
#include "fixtures/vendored_1306_lib.h"

int spelling_dedup_1306_call_a(void) {
    return v1306_use_cfg(3);
}
