// CCCC_FLAGS: --testing
// Suite: per-test warning and safety flags (#612), plus parse-and-ignore
// coverage for the per-function optimize attribute (the VM has no optimiser).

#pragma cccc suite begin "per_test_flags"

// --- Warning flag tests ---

// -Wpedantic per-test: pedantic-clean code compiles and runs.
[[cccc::test(return = 42, flags = "-Wpedantic")]]
int test_warn_wpedantic_clean(void) {
    int x = 42;
    return x;
}

// -Wno-unused per-test: suppress unused-variable warning.
[[cccc::test(return = 42, flags = "-Wno-unused")]]
int test_warn_wno_unused(void) {
    int unused_var = 0;
    return 42;
}

// -Wall per-test: code with no warnings compiles cleanly.
[[cccc::test(return = 42, flags = "-Wall")]]
int test_warn_wall_clean(void) {
    return 6 * 7;
}

// Two consecutive tests with different warning flags trigger separate
// recompiles.
[[cccc::test(return = 42, flags = "-Wall")]]
int test_warn_wall_consecutive_1(void) {
    return 42;
}

[[cccc::test(return = 42, flags = "-Wall -Wpedantic")]]
int test_warn_wall_pedantic(void) {
    return 42;
}

// Back to no flags: recompile to base state.
[[cccc::test(return = 42)]]
int test_warn_back_to_base(void) {
    return 42;
}

// Unrecognized return= operand with -Wattributes per-test: assertion is
// silently skipped (ret_kind = RET_NONE), test still passes (#350, #621).
[[cccc::test(return = GREEN, flags = "-Wattributes")]]
int test_unrecognized_return_operand(void) {
    return 42;
}

#pragma cccc suite end

// [from test_attr_flags_bounds.c]
// Per-test bounds-checking via flags= attribute (ticket #356).
#pragma cccc suite begin "per_test_flags/bounds"

[[cccc::test(flags = "--bounds-checks", return = 6)]]
int test_bounds_in_bounds(void) {
    int arr[4] = {1, 2, 3, 6};
    return arr[3];
}

[[cccc::test(flags = "-b", return = 10)]]
int test_bounds_short_flag(void) {
    int arr[3] = {10, 20, 30};
    return arr[0];
}

[[cccc::test(return = 99)]]
int test_bounds_after_flagged(void) {
    return 99;
}

#pragma cccc suite end

// [from test_attr_flags_mixed.c]
// Lazy recompile with mixed flagged/unflagged tests (ticket #356).
#pragma cccc suite begin "per_test_flags/mixed"

[[cccc::test(flags = "--bounds-checks --overflow-checks", return = 7)]]
int test_shared_flags_first(void) {
    return 3 + 4;
}

[[cccc::test(flags = "--bounds-checks --overflow-checks", return = 12)]]
int test_shared_flags_second(void) {
    return 12;
}

[[cccc::test(return = 1)]]
int test_unflagged_between(void) {
    return 1;
}

[[cccc::test(flags = "--safety=basic", return = 9)]]
int test_different_flags(void) {
    return 9;
}

[[cccc::test(return = 55)]]
int test_unflagged_at_end(void) {
    return 55;
}

#pragma cccc suite end

// [from test_attr_flags_optimise.c]
// Per-test safety and combined flags (ticket #356).
#pragma cccc suite begin "per_test_flags/safety"

[[cccc::test(flags = "--safety=standard", return = 3)]]
int test_safety_standard(void) {
    int a = 1, b = 2;
    return a + b;
}

[[cccc::test(flags = "-2", return = 100)]]
int test_safety_short(void) {
    return 100;
}

[[cccc::test(return = 0)]]
int test_baseline_after_flagged(void) {
    return 0;
}

#pragma cccc suite end

// [from test_attr_optimize.c]
// Per-function optimize attribute: the VM has no optimiser, so these are
// parsed, syntax-checked, warn-and-ignored (GCC/Clang source compatibility).
// Both spellings must still compile and the functions must still work.
#pragma cccc suite begin "per_fn_optimize"

__attribute__((optimize("O2"))) static int gnu_attr_add_opt(int a, int b) {
    return a + b;
}

[[cccc::optimize(3)]]
static long c23_attr_mul(long a, long b) {
    return a * b;
}

__attribute__((optimize("-O3"))) static int gcc_dash_attr(int a, int b) {
    return a - b;
}

[[cccc::optimize("O2")]]
static int c23_str_attr_fn(int x) {
    return x + 1;
}

[[cccc::test]]
static void test_optimize_attr_ignored_but_compiles(void) {
    AssertEq(gnu_attr_add_opt(3, 4), 7);
    AssertEq(c23_attr_mul(6, 7), 42);
    AssertEq(gcc_dash_attr(10, 4), 6);
    AssertEq(c23_str_attr_fn(41), 42);
}

#pragma cccc suite end
