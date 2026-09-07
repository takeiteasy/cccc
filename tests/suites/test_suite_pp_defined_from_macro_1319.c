// CCCC_FLAGS: --testing
//
// #1319 / #1318: a macro invoked inside a #if/#elif constant expression can
// itself expand *to* `defined(...)` or a `__has_*(...)` operator -- e.g.
// Apple SDK headers wrap `defined(__DRIVERKIT_VERSION_MIN_REQUIRED)` inside
// their own compat macros (pthread.h's
// _PTHREAD_SWIFT_IMPORTER_NULLABILITY_COMPAT(), secure/_string.h's
// __is_modern_darwin()). The #if evaluator's defined/__has_* rewrite pass
// (read_const_expr(), src/preprocess.c) used to run once, before macro
// expansion -- an operator that only appeared *after* expansion fell
// through to the generic identifier -> "0" rule and corrupted
// `defined(FOO)` into `0(0)`, which the parser rejected with a bare
// "not a function" (no file/line pointing at the real cause).
//
// Fixed by (1) protecting a defined/__has_*'s operand from expansion while
// preprocess2() is evaluating a #if/#elif expression, and (2) re-running
// the rewrite pass on preprocess2()'s output before the generic identifier
// fallback. Both directions are needed -- see src/preprocess.c's
// rewrite_pp_operators()/eval_const_expr() comments.
//
// This file must actually compile (a #error below would abort compilation)
// -- that IS the assertion, mirroring the mechanism test_suite_macros.c
// uses for the #584 macro-table regression.

// --- Minimal repro from the ticket investigation: a function-like macro
// whose body is a `defined`/`!defined` chain, invoked from #if. ---
#define PP_1319_COMPAT()                                                       \
    defined(PP_1319_UNDEF_A) &&                                                \
        (!defined(PP_1319_UNDEF_B) || (PP_1319_UNDEF_B < 1))
#if !PP_1319_COMPAT()
// Expected: PP_1319_UNDEF_A is undefined, so COMPAT() is 0, !0 is true.
#define PP_1319_TAKEN_1 1
#endif
#ifndef PP_1319_TAKEN_1
#error "#1319: defined() reached via macro expansion evaluated wrong"
#endif

// --- Same shape, but one operand names a macro that IS defined -- guards
// against expanding the operand instead of just testing its definedness
// (that would corrupt `defined(FOO)` into `defined(1)`, a different bug
// than #1319/#1318 but a natural thing for a naive fix to introduce). ---
#define PP_1319_DEFINED_VAL 42
#define PP_1319_COMPAT2()   defined(PP_1319_DEFINED_VAL)
#if PP_1319_COMPAT2()
#define PP_1319_TAKEN_2 1
#endif
#ifndef PP_1319_TAKEN_2
#error                                                                         \
    "#1319: defined() of a live macro, reached via expansion, evaluated wrong"
#endif

// --- __has_builtin/__has_attribute reached through a macro expansion,
// exercising the same rewrite path for the other operator family. CCCC
// doesn't recognize `__has_builtin`/`__has_attribute` as #ifdef-able macro
// names (only as the special #if operators they are), so -- unlike the
// real SDK macros this mirrors -- there's no `#ifdef __has_builtin`
// fallback to guard with; call the operator directly, which is exactly
// the shape that matters here (a macro whose body invokes the operator).
#define PP_1319_SUPPORTS(b) __has_builtin(b)
#if PP_1319_SUPPORTS(__builtin_memcpy)
#define PP_1319_TAKEN_3 1
#endif
#ifndef PP_1319_TAKEN_3
#error "#1319: __has_builtin() reached via macro expansion evaluated wrong"
#endif

#define PP_1319_HAS_ATTR(a) __has_attribute(a)
// unused/deprecated is portable enough to be a reliable "supported" probe
// across every compiler CCCC's own __has_attribute table backs (man/
// ATTRIBUTES.md); this file only needs *a* case that reaches 1, not this
// exact one.
#if PP_1319_HAS_ATTR(unused)
#define PP_1319_TAKEN_4 1
#endif
#ifndef PP_1319_TAKEN_4
#error "#1319: __has_attribute() reached via macro expansion evaluated wrong"
#endif

[[cccc::test]]
void test_pp_defined_from_macro_1319(void) {
    AssertEq(PP_1319_TAKEN_1, 1);
    AssertEq(PP_1319_TAKEN_2, 1);
    AssertEq(PP_1319_TAKEN_3, 1);
    AssertEq(PP_1319_TAKEN_4, 1);
}
