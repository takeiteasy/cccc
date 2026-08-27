// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: uses __builtin_pc_function_name, which resolves a VM
// bytecode offset via the VM's own symbol table and has no native
// equivalent (#969)
//
// Tests for __builtin_return_address(n) and __builtin_pc_function_name that
// depend on a *non-tail-call* two-deep helper chain (see below). Split out of
// test_builtin_return_address.c / test_builtin_pc_symbolize.c (#835). Tail
// calls are eliminated unconditionally, so `return inner();` would become a
// CALLT and collapse the caller's frame -- already covered, on purpose, by
// test_builtin_return_address_callt.c. Each helper here instead assigns the
// call result to a local and returns that (`v = inner(); return v;`), which
// is not a tail-call position (can_emit_tail_call only accepts a bare
// ND_FUNCALL), so the two-deep frame chain these tests assume is preserved.
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

static void *inner_ra0(void) {
    return __builtin_return_address(0);
}
static void *inner_ra1(void) {
    return __builtin_return_address(1);
}
static void *inner_ra2(void) {
    return __builtin_return_address(2);
}

// `v = inner(); return v;` — deliberately NOT a tail-call position, so the
// caller's frame survives and the two-deep chain stays two-deep.
static void *outer_calls_inner_ra0(void) {
    void *v = inner_ra0();
    return v;
}
static void *outer_calls_inner_ra1(void) {
    void *v = inner_ra1();
    return v;
}
static void *outer_calls_inner_ra2(void) {
    void *v = inner_ra2();
    return v;
}

// Three-level chain for testing level-1 __builtin_pc_function_name lookup:
//   test_fn → outer_calls_name_inner → name_inner_ra1
// Inside name_inner_ra1:
//   level 0 → return addr in outer_calls_name_inner body
//   level 1 → return addr in test_fn body
static const char *name_inner_ra1(void) {
    void       *ra = __builtin_return_address(1);
    const char *n  = __builtin_pc_function_name(ra);
    return n;
}
static const char *outer_calls_name_inner(void) {
    const char *v = name_inner_ra1();
    return v;
}

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
