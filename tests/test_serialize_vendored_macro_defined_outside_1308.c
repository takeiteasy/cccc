// #1308 (residual of #1301): function_is_header_supplied()'s
// (src/serialize_program.c) EARLY gate tested `obj->tok`'s raw file
// identity directly, unconditionally, ahead of EITHER the is_static or
// external-linkage branch below it -- including the external-linkage
// branch, which only ever consults `obj->body->tok`'s own expansion site
// (walked further down, the #1301 fix) and never re-derives anything from
// `obj->tok` at all.
//
// For a macro-token-pasted declarator (`#define DECORATE(name)
// prefix_##name`) whose #define lives OUTSIDE the header it's invoked
// from, the pasted token's raw `->file` is the paste's own synthetic file
// naming where the `##` operator itself was WRITTEN (the includer), not
// where `DECORATE(...)` was invoked (the header) -- confirmed via
// standalone repro. Since the includer is a command-line input, the early
// gate wrongly returned false immediately, before the external-linkage
// branch's own correct body_exp-based check ever ran, so the header's own
// captured/replayed definition (via IMPLEMENTATION-style configuration)
// was independently re-serialized a SECOND time from CCCC's own AST: a
// host "redefinition" error under -c=native.
//
// Found via #1132's self-hosting spike round 14: src/stdlib/format_printf.c
// defines `STB_SPRINTF_DECORATE(name)` OUTSIDE stb_sprintf.h (unlike
// #1301's own fixture, tests/fixtures/vendored_1301_lib.h, whose
// V1301_DECORATE is defined INSIDE the header -- the paste's synthetic
// file happens to already equal the correct answer there, which is why
// #1301's own test never caught this). "redefinition of
// 'cccc_stbsp_vsprintfcb'" and five siblings were the last errors standing
// once #1305/#1306/#1307 cleared every #include-dedup class this round.
//
// Fix: the early gate now walks token_expansion_site() on obj->tok too --
// identity for an ordinary (non-macro) token, matching every other
// ->origin walk in this file -- so it asks the same question either
// branch would ask, instead of a stricter one only the is_static branch
// used to redo for itself.
//
// tools/comptime_native_smoke.py's
// case_vendored_macro_defined_outside_1308 is the load-bearing
// VM-42-to-native-42 proof -- a plain -m shape assertion alone doesn't
// invoke the host compiler and can't see the "redefinition" this fixes.
// CCCC_FLAGS: tests/fixtures/vendored_macro_defined_outside_1308_b.c
#define DECORATE_1308(name) v1308_##name
#define VENDORED_1308_IMPLEMENTATION
#include "fixtures/vendored_1308_macro_outside_lib.h"

int vendored_macro_outside_1308_use(void) {
    return v1308_add(41);
}
