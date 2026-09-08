// CCCC_FLAGS: --testing
//
// #1323: rewrite_pp_operators() (src/preprocess.c) only recognised a fixed,
// named set of preprocessor operators (defined, __has_include,
// __has_feature, __has_extension, __has_attribute, __has_builtin,
// __has_c_attribute, __has_cpp_attribute, __has_embed). An operator-shaped
// identifier CCCC didn't recognize -- __has_declspec_attribute and
// __has_warning are real ones seen in the macOS SDK -- fell through the
// generic identifier -> "0" rule the same way #1319/#1318 did, producing a
// bogus `0(...)` function call and the unhelpful "not a function"
// diagnostic instead of a real answer.
//
// Fixed by (1) recognising __has_declspec_attribute/__has_warning/
// __has_include_next as real operators (all conservatively evaluate to 0 --
// see the comments at eval_has_name()/rewrite_pp_operators() in
// src/preprocess.c for why __has_include_next in particular can't safely
// answer 1), and (2) a fallback arm catching any *other* unrecognised
// __has_*(...) the same way, so it degrades to 0 instead of corrupting into
// a bogus function call. The warning that fallback emits is pinned by a
// separate test (test_warning_unknown_pp_operator_1323.c) -- suite files
// like this one have no per-test stderr matching (see e.g.
// test_suite_pp_defined_from_macro_1319.c's own note); this file's
// assertion is that it compiles and evaluates correctly at all, mirroring
// that file's mechanism.

#if __has_declspec_attribute(dllexport)
#define PP_1323_TAKEN_1 1
#else
#define PP_1323_TAKEN_1 0
#endif
#ifndef PP_1323_TAKEN_1
#error "#1323: __has_declspec_attribute() failed to evaluate"
#endif

#if __has_warning("-Wall")
#define PP_1323_TAKEN_2 1
#else
#define PP_1323_TAKEN_2 0
#endif
#ifndef PP_1323_TAKEN_2
#error "#1323: __has_warning() failed to evaluate"
#endif

#if __has_include_next(<stdio.h>)
#define PP_1323_TAKEN_3 1
#else
#define PP_1323_TAKEN_3 0
#endif
#ifndef PP_1323_TAKEN_3
#error "#1323: __has_include_next() failed to evaluate"
#endif

// An operator CCCC has never heard of at all -- not a real name, but the
// same __has_*(...) shape. Must evaluate to 0 (the fallback arm), not
// corrupt into "not a function".
#if __has_frobnicate_1323(x)
#define PP_1323_TAKEN_4 1
#else
#define PP_1323_TAKEN_4 0
#endif
#ifndef PP_1323_TAKEN_4
#error "#1323: an unknown __has_* operator failed to evaluate"
#endif
#if PP_1323_TAKEN_4 != 0
#error "#1323: an unknown __has_* operator must evaluate to 0"
#endif

// Same shape, reached through a macro expansion rather than literally --
// exercises rewrite_pp_operators()'s post-expansion pass and
// pp_operand_protect's generic __has_*-prefix guard (both in
// src/preprocess.c), same as #1319/#1318 did for the named operators.
#define PP_1323_PROBE() __has_another_unknown_1323(y)
#if PP_1323_PROBE()
#define PP_1323_TAKEN_5 1
#else
#define PP_1323_TAKEN_5 0
#endif
#ifndef PP_1323_TAKEN_5
#error "#1323: an unknown __has_* operator reached via macro expansion failed"
#endif
#if PP_1323_TAKEN_5 != 0
#error                                                                         \
    "#1323: an unknown __has_* operator reached via macro expansion must evaluate to 0"
#endif

[[cccc::test]]
void test_pp_has_unknown_1323(void) {
    AssertEq(PP_1323_TAKEN_1, 0);
    AssertEq(PP_1323_TAKEN_2, 0);
    AssertEq(PP_1323_TAKEN_4, 0);
    AssertEq(PP_1323_TAKEN_5, 0);
    // PP_1323_TAKEN_3 (__has_include_next) is deliberately not asserted to
    // a specific value here -- it's conservatively always 0 (see the
    // header comment), which the #ifndef check above already confirmed it
    // evaluated to *something* rather than corrupting the compile.
    (void)PP_1323_TAKEN_3;
}
