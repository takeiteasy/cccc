// CCCC_FLAGS: --testing
// Tests for __builtin_return_address(n)
// Level 0 reads bp[+1] (the return address pushed by CALL before ENT3).
// The returned value is a VM bytecode offset (Pc/uint32_t), not a host address.
// Returns NULL past the outermost frame.
//
// Frame structure notes:
// Each test function is run via cc_run_at(), so it is the outermost frame and
// its own return address (bp[+1]) is the sentinel value 0.  A single helper
// called from the test function has:
//   level 0 → nonzero  (return addr back into test fn)
//   level 1 → 0        (test fn's sentinel ret_addr)
//
// For level 1 to be nonzero we need a helper chain:
//   test_fn → outer() → inner()
// Inside inner():
//   level 0 → nonzero  (return to outer)
//   level 1 → nonzero  (return to test_fn, from the CALL inside test_fn)
//   level 2 → 0        (test_fn's sentinel)

#include <stddef.h>

// Single-depth helpers ─────────────────────────────────────────────────────

static void *get_ra0(void) { return __builtin_return_address(0); }
static void *get_ra1(void) { return __builtin_return_address(1); }

// Two-depth helpers (inner called from outer, outer called from test fn) ──

static void *inner_ra0(void) { return __builtin_return_address(0); }
static void *inner_ra1(void) { return __builtin_return_address(1); }
static void *inner_ra2(void) { return __builtin_return_address(2); }

static void *outer_calls_inner_ra0(void) { return inner_ra0(); }
static void *outer_calls_inner_ra1(void) { return inner_ra1(); }
static void *outer_calls_inner_ra2(void) { return inner_ra2(); }

// ─── Type acceptance ───────────────────────────────────────────────────────

[[cccc::test]]
void test_return_address_is_ptr(void) {
    void *ra = __builtin_return_address(0);
    // Type must be void* — just ensure it compiles and is usable as a pointer.
    (void)ra;
}

// ─── Level 0: non-null from inside a helper ──────────────────────────────
// The helper has a real caller (the test fn), so its return address is nonzero.

[[cccc::test]]
void test_return_address_level0_nonzero(void) {
    void *ra = get_ra0();
    AssertNotNull(ra);
}

// ─── Level 0: test fn itself is outermost → sentinel 0 ───────────────────

[[cccc::test]]
void test_return_address_test_fn_is_outermost(void) {
    // Each [[cccc::test]] is invoked via cc_run_at which pushes a sentinel 0
    // return address.  So the test function's own bp[+1] is 0 (NULL).
    void *ra = __builtin_return_address(0);
    AssertNull(ra);
}

// ─── Distinctness: two different call sites ───────────────────────────────
// get_ra0() called from two different sites returns two different Pcs.
// A NULL stub would return the same value (0) from both.

[[cccc::test]]
void test_return_address_distinct_call_sites(void) {
    void *ra1 = get_ra0();  // call site 1
    void *ra2 = get_ra0();  // call site 2
    AssertNeq((long long)ra1, (long long)ra2);
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

// ─── NULL past outermost frame (very deep level) ─────────────────────────

[[cccc::test]]
void test_return_address_out_of_bounds_is_null(void) {
    // Walking an unreachably large level should exhaust the stack safely
    // and return NULL, not crash.
    void *ra = __builtin_return_address(9999);
    AssertNull(ra);
}
