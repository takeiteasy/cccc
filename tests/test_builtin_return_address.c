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
// Level-1+ lookups through a two-deep test_fn → outer() → inner() helper
// chain (#835) live in test_builtin_return_address_notco.c instead: each
// `return inner();` there is a tail call at -O1+, and CALLT elides the
// intermediate frame that those tests' level-1/level-2 lookups depend on
// walking. This file's tests are unaffected by that (no tail-position helper
// chain), so it keeps full opt-level coverage.

#include <stddef.h>

// Single-depth helpers ─────────────────────────────────────────────────────

static void *get_ra0(void) {
    return __builtin_return_address(0);
}
static void *get_ra1(void) {
    return __builtin_return_address(1);
}

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
    void *ra1 = get_ra0(); // call site 1
    void *ra2 = get_ra0(); // call site 2
    AssertNeq((long long)ra1, (long long)ra2);
}

// ─── NULL past outermost frame (very deep level) ─────────────────────────

[[cccc::test]]
void test_return_address_out_of_bounds_is_null(void) {
    // Walking an unreachably large level should exhaust the stack safely
    // and return NULL, not crash.
    void *ra = __builtin_return_address(9999);
    AssertNull(ra);
}
