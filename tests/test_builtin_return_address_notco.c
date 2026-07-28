// CCCC_FLAGS: --testing -O0
// CCCC_MATRIX_SKIP: the two-deep test_fn -> outer() -> inner() helper chain
//   requires no tail-call collapse; -O1+ behaviour is covered by
//   test_builtin_return_address_callt.c
//
// Tests for __builtin_return_address(n) and __builtin_pc_function_name that
// depend on a *non-tail-call* two-deep helper chain (see below). Split out of
// test_builtin_return_address.c / test_builtin_pc_symbolize.c (#835): at
// `-O1`+, `return inner();` in each of these helpers is a textbook tail call,
// and CALLT (gated `opt_level >= 1`, src/codegen.c's can_emit_tail_call)
// elides the caller's stack frame in that position. That collapses the
// two-deep chain these tests assume down to one frame, so the level-1 lookups
// here would observe the *tail-called* semantics instead — already covered,
// on purpose, by test_builtin_return_address_callt.c. Pinning to -O0 keeps
// this file testing the plain (non-TCO) frame-walk behaviour its comments
// describe.
//
// Frame structure notes:
// Each test function is run via cc_run_at(), so it is the outermost frame and
// its own return address (bp[+1]) is the sentinel value 0.
//
// For a two-deep chain:
//   test_fn → outer() → inner()
// Inside inner():
//   level 0 → nonzero  (return to outer)
//   level 1 → nonzero  (return to test_fn, from the CALL inside test_fn)
//   level 2 → 0        (test_fn's sentinel)

#include <stddef.h>

// Two-depth helpers (inner called from outer, outer called from test fn) ──

static void *inner_ra0(void) { return __builtin_return_address(0); }
static void *inner_ra1(void) { return __builtin_return_address(1); }
static void *inner_ra2(void) { return __builtin_return_address(2); }

static void *outer_calls_inner_ra0(void) { return inner_ra0(); }
static void *outer_calls_inner_ra1(void) { return inner_ra1(); }
static void *outer_calls_inner_ra2(void) { return inner_ra2(); }

// Three-level chain for testing level-1 __builtin_pc_function_name lookup:
//   test_fn → outer_calls_name_inner → name_inner_ra1
// Inside name_inner_ra1:
//   level 0 → return addr in outer_calls_name_inner body
//   level 1 → return addr in test_fn body
static const char *name_inner_ra1(void) {
    void *ra = __builtin_return_address(1);
    return __builtin_pc_function_name(ra);
}
static const char *outer_calls_name_inner(void) { return name_inner_ra1(); }

// ─── Level 1 from two-deep chain is nonzero ──────────────────────────────
// test_fn → outer_calls_inner_ra1 → inner_ra1
// Inside inner_ra1, level 1 is outer_calls_inner_ra1's return address
// (the CALL back to test_fn) — nonzero.

[[cccc::test]]
void test_return_address_level1_nonzero(void) {
    void *ra1 = outer_calls_inner_ra1();
    AssertNotNull(ra1);
}

// ─── Level 2 from two-deep chain is the test fn sentinel (0) ────────────

[[cccc::test]]
void test_return_address_level2_is_sentinel(void) {
    void *ra2 = outer_calls_inner_ra2();
    AssertNull(ra2);
}

// ─── Level 0 vs level 1 differ (both from inside two-deep chain) ─────────

[[cccc::test]]
void test_return_address_levels_differ(void) {
    void *ra0 = outer_calls_inner_ra0();
    void *ra1 = outer_calls_inner_ra1();
    // ra0 = ret addr inside outer_calls_inner_ra0 (back to outer)
    // ra1 = ret addr inside outer_calls_inner_ra1 (back to test_fn)
    // Both nonzero and different (different call depths).
    AssertNotNull(ra0);
    AssertNotNull(ra1);
    AssertNeq((long long)ra0, (long long)ra1);
}

// ─── __builtin_pc_function_name resolves an outer (level-1) caller ───────

[[cccc::test]]
void test_pc_function_name_resolves_outer_caller(void) {
    // Three-level chain: test_fn → outer_calls_name_inner → name_inner_ra1
    // ra at level 1 inside name_inner_ra1 falls inside *this* test function's
    // body, so the function name must be this test function's name.
    const char *fn = outer_calls_name_inner();
    AssertNotNull(fn);
    AssertStrEq(fn, "test_pc_function_name_resolves_outer_caller");
}
