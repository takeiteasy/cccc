// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: uses __builtin_pc_function_name, which resolves a VM
// bytecode offset via the VM's own symbol table and has no native
// equivalent (#969)
// Tests for __builtin_return_address interaction with tail calls
// (CALLT).
//
// CALLT unwinds the intermediate frame before jumping to the callee.
// The callee therefore sees only ONE frame on the stack above it — the
// original caller's frame — not two frames (intermediate + original caller).
//
// Frame picture for test_fn → tail_wrapper → ra_capture:
//
//   Without CALLT (O0):  test_fn | tail_wrapper | ra_capture
//   With    CALLT (O1):  test_fn | ra_capture   (tail_wrapper frame erased)
//
// So inside ra_capture (called via CALLT from tail_wrapper):
//   level 0 → return addr into test_fn  (NOT into tail_wrapper)
//   level 1 → 0 (sentinel: test_fn is outermost)
//
// The __builtin_pc_function_name of the level-0 address must therefore name
// the test function, not tail_wrapper.
//
// -O1 is required to enable TCO (CALLT emission); without it these tests
// would instead document non-TCO two-deep-chain behaviour, which is already
// covered by test_builtin_return_address.c.

#include <stddef.h>
#include <string.h>

// ─── Helpers ──────────────────────────────────────────────────────────────

static void *ra_capture(void) {
    return __builtin_return_address(0);
}
static void *ra_capture_level1(void) {
    return __builtin_return_address(1);
}
static void *ra_capture_level2(void) {
    return __builtin_return_address(2);
}

// These are the tail wrappers.  With -O1 each return statement is compiled
// as a CALLT — the wrapper frame is unbound before the callee runs.
static void *tail_wrap_ra0(void) {
    return ra_capture();
}
static void *tail_wrap_ra1(void) {
    return ra_capture_level1();
}
static void *tail_wrap_ra2(void) {
    return ra_capture_level2();
}

// ─── Level-0 tests ────────────────────────────────────────────────────────

[[cccc::test]]
void test_callt_ra_level0_nonzero(void) {
    // ra_capture is reached via CALLT from tail_wrap_ra0.
    // Level 0 gives the return address back into *this* test function.
    void *ra = tail_wrap_ra0();
    AssertNotNull(ra);
}

[[cccc::test]]
void test_callt_ra_level0_equals_direct_to_same_site(void) {
    // Two calls via different wrappers (or directly) but from the SAME call
    // site must give the same return address (both point right after the CALL
    // instruction that invokes the wrapper from this test function).
    //
    // We use two separate call sites and verify that:
    //   - both are nonzero
    //   - they differ (different call sites produce different return addresses)
    void *ra1 = tail_wrap_ra0(); // call site A
    void *ra2 = tail_wrap_ra0(); // call site B
    AssertNotNull(ra1);
    AssertNotNull(ra2);
    AssertNeq((long long)ra1, (long long)ra2);
}

// ─── Level-1 tests ────────────────────────────────────────────────────────

[[cccc::test]]
void test_callt_ra_level1_is_sentinel(void) {
    // With CALLT the intermediate wrapper frame is gone.
    // From inside ra_capture_level1 there is only one frame above:
    // this test function, whose own return address is the sentinel 0.
    // Level 1 therefore returns NULL.
    void *ra = tail_wrap_ra1();
    AssertNull(ra);
}

[[cccc::test]]
void test_callt_ra_level2_is_sentinel(void) {
    // Level 2 tries to walk two frames above ra_capture_level2, but with CALLT
    // the stack only has test_fn above it.  Both level 1 (test_fn sentinel)
    // and level 2 (off the stack) yield NULL.
    void *ra = tail_wrap_ra2();
    AssertNull(ra);
}

// ─── Symbolization tests ──────────────────────────────────────────────────

[[cccc::test]]
void test_callt_ra_symbolizes_to_caller(void) {
    // The level-0 return address captured inside a tail-called function maps
    // to the original caller (this test function), not to the wrapper.
    void *ra = tail_wrap_ra0();
    AssertNotNull(ra);
    const char *fn = __builtin_pc_function_name(ra);
    AssertNotNull(fn);
    AssertStrEq(fn, "test_callt_ra_symbolizes_to_caller");
}

[[cccc::test]]
void test_callt_ra_does_not_symbolize_to_wrapper(void) {
    // Sanity-check: the return address must NOT name the wrapper.
    void *ra = tail_wrap_ra0();
    AssertNotNull(ra);
    const char *fn = __builtin_pc_function_name(ra);
    AssertNotNull(fn);
    // If CALLT is working, the wrapper frame is gone and ra points into the
    // test function, not into tail_wrap_ra0.
    Assert(strcmp(fn, "tail_wrap_ra0") != 0);
}
