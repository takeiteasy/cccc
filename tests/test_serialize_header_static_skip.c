// CCCC_FLAGS: tests/fixtures/header_static_skip_999_a.c -m
// CCCC_C4_SKIP: multi-source compile, not a single-TU bytecode round-trip
// CCCC_EXPECT_STDOUT: int header_static_skip_999_a\(void\)
// CCCC_REJECT_STDOUT: static int header_static_skip_999_helper
//
// #999: a `static` function defined in a plain #include'd header (no
// #pragma once / #ifndef guard -- tests/fixtures/header_static_skip_999.h)
// and included from two translation units (fixture TU1, this file's TU2)
// used to be re-emitted once per TU by cc_serialize_program, since
// internal-linkage functions are deliberately left uncanonicalized across
// TUs by cc_link_progs (#957) -- two Objs, two definitions, a
// "redefinition" error from the downstream host compiler. This test only
// checks -m's shape (the definition must not appear at all in this TU's
// portion of the output, since the header's own #include already supplies
// it); tools/comptime_native_smoke.py's dandy-pattern case is what proves
// the resulting -c=native output actually links.
#include "fixtures/header_static_skip_999.h"

int header_static_skip_999_a(void);
int header_static_skip_999_b(void) { return header_static_skip_999_helper(21); }

int main(void) { return header_static_skip_999_a() + header_static_skip_999_b(); }
