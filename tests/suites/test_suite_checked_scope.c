// CCCC_FLAGS: --testing
// Checked regions (#485): [[cccc::checked]]/[[cccc::unchecked]] on a function
// definition or compound statement, and #pragma cccc checked/unchecked
// begin/end. These are pure parse/type-check diagnostics -- always on,
// independent of --checked-pointers (deliberately not passed here) -- see
// man/SAFETY.md's "Checked Regions" section. Positive cases live here
// (single compiling file); compile-error cases cannot share a compiling
// file with these (--testing=native doesn't support in-suite negative
// tests, #1033 v1) and live in standalone tests/test_checked_scope_*.c
// files instead, mirroring test_suite_checked_pointers.c's own split.

#include <stdio.h>

// ---------------------------------------------------------------------
// Basic function/block attribute forms, and nesting.
// ---------------------------------------------------------------------

[[cccc::checked]]
int checked_array_sum(void) {
    int n                                  = 4;
    int *[[cccc::array, cccc::count(n)]] a = (int[4]){1, 2, 3, 4};
    return a[0] + a[1] + a[2] + a[3];
}

[[cccc::test]]
void test_checked_function_body(void) {
    AssertEq(checked_array_sum(), 10);
}

[[cccc::checked]]
int checked_with_unchecked_escape(void) {
    int n                                  = 1;
    int *[[cccc::array, cccc::count(n)]] a = (int[1]){41};
    int result;
    [[cccc::unchecked]] {
        int *raw = (int *)a; // fine -- inside the escape hatch
        result   = raw[0] + 1;
    }
    return result;
}

[[cccc::test]]
void test_unchecked_block_nested_in_checked_function(void) {
    AssertEq(checked_with_unchecked_escape(), 42);
}

[[cccc::checked]]
int three_deep_nesting(void) {
    int total = 0;
    [[cccc::unchecked]] {
        int *p = &total;                 // fine -- unchecked block
        [[cccc::checked]] {
            int *[[cccc::single]] q = p; // fine -- checked again
            *q                      = 40;
        }
        *p += 2;
    }
    return total;
}

[[cccc::test]]
void test_three_deep_alternating_nesting(void) {
    AssertEq(three_deep_nesting(), 42);
}

// ---------------------------------------------------------------------
// What is explicitly NOT banned inside a checked region.
// ---------------------------------------------------------------------

[[cccc::checked]]
int array_kind_with_no_bounds_form(void) {
    int arr[3]             = {1, 2, 3};
    int *[[cccc::array]] a = arr; // legal-but-unchecked -- not banned
    return a[0] + a[1] + a[2];
}

[[cccc::test]]
void test_array_kind_no_bounds_form_allowed(void) {
    AssertEq(array_kind_with_no_bounds_form(), 6);
}

[[cccc::checked]]
int bounds_unknown_escape_hatch(void) {
    int arr[3]                                    = {10, 20, 12};
    int *[[cccc::array, cccc::bounds(unknown)]] a = arr;
    return a[0] + a[1] + a[2];
}

[[cccc::test]]
void test_bounds_unknown_allowed(void) {
    AssertEq(bounds_unknown_escape_hatch(), 42);
}

[[cccc::checked]]
int checked_calls_libc(void) {
    // Calling into an unchecked-prototype libc function is allowed --
    // interop is a documented v1 gap, not enforced. puts() takes a
    // `const char *`, an unchecked pointer parameter on puts()'s own
    // (header, bodyless) prototype -- but that prototype's parameter never
    // reaches create_param_lvars() (no body), and the cast/decl ban is only
    // about what THIS file declares, not what it calls.
    return puts("ok");
}

[[cccc::test]]
void test_libc_call_from_checked_region_allowed(void) {
    Assert(checked_calls_libc() >= 0);
}

// ---------------------------------------------------------------------
// Region attribute spellings.
// ---------------------------------------------------------------------

__attribute__((checked)) int gnu_spelling_checked(void) {
    int *[[cccc::single]] p = 0;
    (void)p;
    return 42;
}

[[cccc::test]]
void test_gnu_attribute_spelling(void) {
    AssertEq(gnu_spelling_checked(), 42);
}

@checked int at_prefix_spelling(void) {
    int *[[cccc::single]] p = 0;
    (void)p;
    return 42;
}

[[cccc::test]]
void test_at_prefix_spelling(void) {
    AssertEq(at_prefix_spelling(), 42);
}

// ---------------------------------------------------------------------
// State does not leak: after a checked function/block ends, ordinary
// unchecked code right after is unaffected.
// ---------------------------------------------------------------------

[[cccc::checked]]
void a_checked_function(void) {
    int *[[cccc::single]] p = 0;
    (void)p;
}

int an_unchecked_function(void) {
    int *p = 0; // must NOT be rejected -- no region here
    (void)p;
    return 42;
}

[[cccc::test]]
void test_region_does_not_leak_across_functions(void) {
    a_checked_function();
    AssertEq(an_unchecked_function(), 42);
}

int unchecked_after_block(void) {
    [[cccc::checked]] {
        int *[[cccc::single]] q = 0;
        (void)q;
    }
    int *p = 0; // must NOT be rejected -- block region already closed
    (void)p;
    return 42;
}

[[cccc::test]]
void test_region_does_not_leak_across_block(void) {
    AssertEq(unchecked_after_block(), 42);
}
