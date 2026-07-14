// CCCC_FLAGS: --testing
// Tests for __builtin_pc_function_name and __builtin_pc_source_location.
//
// __builtin_pc_function_name(pc) maps a VM bytecode offset (void*) — such as
// the value returned by __builtin_return_address(n) — to the name of the
// enclosing C function.  Returns NULL when the pc is NULL or outside all known
// function ranges.  Available in all builds (no -g required).
//
// __builtin_pc_source_location(pc, &file, &line) maps a VM bytecode offset to
// a source file name and 1-based line number.  Returns 1 on success, 0 when the
// source map is unavailable (requires -g) or the pc is unknown.  On failure
// *file is NULL and *line is 0.
//
// Together with __builtin_return_address these form a composable symbolisation
// pipeline:
//   void *ra = __builtin_return_address(0);
//   const char *fn = __builtin_pc_function_name(ra);
//   const char *file; int line;
//   __builtin_pc_source_location(ra, &file, &line);

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ─── Helper functions ──────────────────────────────────────────────────────
// These capture their own return address so tests can check the mapping back
// to the *caller's* function name.  Since the test functions are [[cccc::test]]
// entries, the caller of each helper *is* the test function.

static void *get_ra0(void) { return __builtin_return_address(0); }

// Returns the function-name string for its own address (level 0 → the call
// site inside the caller, but we want the name of *this* function's body).
// We capture ra at level 0, which gives the return address *back into the caller*
// — that address lies inside the caller's function body, so we can check the
// caller's name.  The helper below captures itself differently for self-naming.
static const char *name_of_caller(void) {
    void *ra = __builtin_return_address(0);
    return __builtin_pc_function_name(ra);
}

// Three-level chain for testing level-1 lookup:
//   test_fn → outer_calls_name_inner → name_inner_ra1
// Inside name_inner_ra1:
//   level 0 → return addr in outer_calls_name_inner body
//   level 1 → return addr in test_fn body
static const char *name_inner_ra1(void) {
    void *ra = __builtin_return_address(1);
    return __builtin_pc_function_name(ra);
}
static const char *outer_calls_name_inner(void) { return name_inner_ra1(); }

// ─── Type acceptance ───────────────────────────────────────────────────────

[[cccc::test]]
void test_pc_function_name_type(void) {
    void *ra = get_ra0();
    const char *fn = __builtin_pc_function_name(ra);
    // Just ensure it compiles and returns a const char* or NULL.
    (void)fn;
}

[[cccc::test]]
void test_pc_source_location_type(void) {
    void *ra = get_ra0();
    const char *file = NULL;
    int line = 0;
    int ok = __builtin_pc_source_location(ra, &file, &line);
    // Ensure it compiles; return value is 0 without -g.
    (void)ok;
}

// ─── NULL / sentinel pc ───────────────────────────────────────────────────

[[cccc::test]]
void test_pc_function_name_null_pc(void) {
    // NULL pc (outermost sentinel) must return NULL, not crash.
    const char *fn = __builtin_pc_function_name(NULL);
    AssertNull(fn);
}

[[cccc::test]]
void test_pc_source_location_null_pc(void) {
    const char *file = (const char *)0xdeadbeef;
    int line = 99;
    int ok = __builtin_pc_source_location(NULL, &file, &line);
    AssertEq(ok, 0);
    AssertNull(file);
    AssertEq(line, 0);
}

// ─── Function-name lookup ─────────────────────────────────────────────────
// The return address captured inside a helper lands in the *caller's* function
// body, so __builtin_pc_function_name of that ra gives the caller's name.

[[cccc::test]]
void test_pc_function_name_resolves_caller(void) {
    // ra0 is the return address back into *this* test function.
    // The pc falls somewhere inside test_pc_function_name_resolves_caller.
    const char *fn = name_of_caller();
    // Must be non-NULL and equal to this function's name.
    AssertNotNull(fn);
    AssertStrEq(fn, "test_pc_function_name_resolves_caller");
}

[[cccc::test]]
void test_pc_function_name_resolves_outer_caller(void) {
    // Three-level chain: test_fn → outer_calls_name_inner → name_inner_ra1
    // ra at level 1 inside name_inner_ra1 falls inside *this* test function's
    // body, so the function name must be this test function's name.
    const char *fn = outer_calls_name_inner();
    AssertNotNull(fn);
    AssertStrEq(fn, "test_pc_function_name_resolves_outer_caller");
}

// ─── Distinctness ─────────────────────────────────────────────────────────

[[cccc::test]]
void test_pc_function_name_distinct_helpers(void) {
    // Calling name_of_caller from two different test functions returns
    // different strings (the names of those test functions).
    // This test checks that the lookup is not constant.
    const char *fn = name_of_caller();
    AssertNotNull(fn);
    AssertStrEq(fn, "test_pc_function_name_distinct_helpers");
}

// ─── Graceful degradation: source location without -g ─────────────────────
// Without -g the source map is not populated.  The builtin must return 0
// with file=NULL / line=0, and must not crash.

[[cccc::test]]
void test_pc_source_location_degrades_without_g(void) {
    void *ra = get_ra0();
    const char *file = (const char *)0xdeadbeef;
    int line = 99;
    int ok = __builtin_pc_source_location(ra, &file, &line);
    // Without -g ok == 0 and outputs are zeroed.  With -g ok == 1.
    // In this test file (no -g flag) we assert the no-g behaviour.
    AssertEq(ok, 0);
    AssertNull(file);
    AssertEq(line, 0);
}

// ─── Out-of-range / very large pc ─────────────────────────────────────────

[[cccc::test]]
void test_pc_function_name_invalid_pc(void) {
    // A pointer value that is definitely not a valid bytecode offset.
    void *huge = (void *)(uintptr_t)0xffffffff;
    const char *fn = __builtin_pc_function_name(huge);
    // Should return NULL without crashing.
    AssertNull(fn);
}

[[cccc::test]]
void test_pc_source_location_invalid_pc(void) {
    void *huge = (void *)(uintptr_t)0xffffffff;
    const char *file = (const char *)1;
    int line = 1;
    int ok = __builtin_pc_source_location(huge, &file, &line);
    AssertEq(ok, 0);
    AssertNull(file);
    AssertEq(line, 0);
}

// ─── Composability with __builtin_return_address ───────────────────────────

[[cccc::test]]
void test_pc_compose_with_return_address(void) {
    // Full pipeline: capture ra then symbolize.
    void *ra = get_ra0();
    const char *fn = __builtin_pc_function_name(ra);
    // ra points back into *this* test function.
    AssertNotNull(fn);
    AssertStrEq(fn, "test_pc_compose_with_return_address");
}
