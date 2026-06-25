// CCCC_FLAGS: --testing
// Suite: per-test warning and optimisation-pass flags (#612)
// Tests that -W*, -Werror*, -f<pass>, -fno-<pass> are accepted in
// [[cccc::test(flags="...")]] and that #pragma cccc config() accepts
// optimisation-pass keys (fold, peephole, copy_prop, dce, cse, fuse, elim_ext).

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

// Two consecutive tests with different warning flags trigger separate recompiles.
[[cccc::test(return = 42, flags = "-Wall")]]
int test_warn_wall_consecutive_1(void) { return 42; }

[[cccc::test(return = 42, flags = "-Wall -Wpedantic")]]
int test_warn_wall_pedantic(void) { return 42; }

// Back to no flags: recompile to base state.
[[cccc::test(return = 42)]]
int test_warn_back_to_base(void) { return 42; }

// --- Optimisation-pass flag tests (-f<pass> / -fno-<pass>) ---

// Constant folding on: arithmetic should still return the right value.
[[cccc::test(return = 42, flags = "-ffold")]]
int test_f_ffold(void) {
    return 6 * 7;
}

// Constant folding disabled: result still correct (just not folded at compile time).
[[cccc::test(return = 42, flags = "-fno-fold")]]
int test_f_fno_fold(void) {
    return 6 * 7;
}

// Peephole on.
[[cccc::test(return = 42, flags = "-fpeephole")]]
int test_f_fpeephole(void) {
    int x = 42;
    return x;
}

// CSE off, copy propagation on.
[[cccc::test(return = 42, flags = "-fcopy-prop -fno-cse")]]
int test_f_copy_prop_no_cse(void) {
    int a = 21, b = a;
    return a + b;
}

// All major passes together.
[[cccc::test(return = 42, flags = "-ffold -fpeephole -fdce -fcse")]]
int test_f_multi_pass(void) {
    int x = 20, y = 22;
    return x + y;
}

// -O2 combined with -fno-cse (per-test flags compose with safety/opt presets).
[[cccc::test(return = 42, flags = "-O2 -fno-cse")]]
int test_f_opt_plus_no_cse(void) {
    return 42;
}

// --- #pragma cccc config() opt-pass tests ---

// Enable fold via pragma; verify code still returns 42.
[[cccc::test(return = 42)]]
int test_pragma_config_fold(void) {
#pragma cccc config(fold = true)
    return 6 * 7;
}

// Disable CSE via pragma.
[[cccc::test(return = 42)]]
int test_pragma_config_cse_false(void) {
#pragma cccc config(cse = false)
    int a = 21, b = 21;
    return a + b;
}

// Multiple opt-pass keys in one pragma.
[[cccc::test(return = 42)]]
int test_pragma_config_multi_pass(void) {
#pragma cccc config(fold = true, peephole = true, dce = false)
    return 42;
}

// Bare key (no = value) defaults to true.
[[cccc::test(return = 42)]]
int test_pragma_config_bare_key(void) {
#pragma cccc config(fold)
    return 42;
}

// Interleave: pragma opt-pass + existing safety key in same pragma.
[[cccc::test(return = 42)]]
int test_pragma_config_mixed(void) {
#pragma cccc config(fold = true, bounds_checks = false)
    return 42;
}

// Unrecognized return= operand with -Wattributes per-test: assertion is
// silently skipped (ret_kind = RET_NONE), test still passes (#350, #621).
// Warning emission is tested by tests/test_warning_return_unrecognized_operand.c
// (parse-time; cannot be captured via expect_stderr in a suite test).
[[cccc::test(return = GREEN, flags = "-Wattributes")]]
int test_unrecognized_return_operand(void) { return 42; }

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

[[cccc::test(flags = "--optimize=1", return = 9)]]
int test_different_flags(void) {
    return 9;
}

[[cccc::test(return = 55)]]
int test_unflagged_at_end(void) {
    return 55;
}

#pragma cccc suite end

// [from test_attr_flags_optimise.c]
// Per-test optimisation and combined flags (ticket #356).
#pragma cccc suite begin "per_test_flags/optimise"

[[cccc::test(flags = "--optimize=2 --safety=1", return = 2)]]
int test_combined_flags(void) {
    return 1 + 1;
}

[[cccc::test(flags = "--optimize=3", return = 42)]]
int test_opt3(void) {
    int x = 6;
    int y = 7;
    return x * y;
}

[[cccc::test(flags = "-O2", return = 5)]]
int test_opt_short(void) {
    return 2 + 3;
}

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
int test_baseline_after_optimised(void) {
    return 0;
}

#pragma cccc suite end

// [from test_attr_optimize.c]
// Per-function optimize attribute: GCC string form, C23 integer form, @ shorthand.
#pragma cccc suite begin "per_fn_optimize"

__attribute__((optimize("O2")))
static int gnu_attr_add_opt(int a, int b) {
    return a + b;
}

[[cccc::test]]
static void test_gnu_str_attr(void) {
    AssertEq(gnu_attr_add_opt(3, 4), 7);
    AssertEq(gnu_attr_add_opt(0, 0), 0);
    AssertEq(gnu_attr_add_opt(-1, 1), 0);
}

[[cccc::optimize(3)]]
static long c23_attr_mul(long a, long b) {
    return a * b;
}

[[cccc::test]]
static void test_c23_int_attr(void) {
    AssertEq(c23_attr_mul(6, 7), 42);
    AssertEq(c23_attr_mul(0, 100), 0);
    AssertEq(c23_attr_mul(-3, -3), 9);
}

@optimize(1)
static int at_attr_sub(int a, int b) {
    return a - b;
}

[[cccc::test]]
static void test_at_attr(void) {
    AssertEq(at_attr_sub(10, 3), 7);
    AssertEq(at_attr_sub(0, 0), 0);
}

[[cccc::optimize(0)]]
static int o0_attr_fn(int x) {
    return x * 2;
}

[[cccc::test]]
static void test_o0_attr(void) {
    AssertEq(o0_attr_fn(21), 42);
    AssertEq(o0_attr_fn(0), 0);
}

[[cccc::optimize("O2")]]
static int c23_str_attr_fn(int x) {
    return x + 1;
}

[[cccc::test]]
static void test_c23_str_attr(void) {
    AssertEq(c23_str_attr_fn(41), 42);
    AssertEq(c23_str_attr_fn(-1), 0);
}

__attribute__((optimize("-O3")))
static int gcc_dash_attr(int a, int b) {
    return a - b;
}

[[cccc::test]]
static void test_gcc_dash_attr(void) {
    AssertEq(gcc_dash_attr(10, 4), 6);
    AssertEq(gcc_dash_attr(5, 5), 0);
}

static int plain_sq(int x) {
    return x * x;
}

[[cccc::optimize(2)]]
static int opt_double(int x) {
    return x + x;
}

[[cccc::test]]
static void test_mixed_opt(void) {
    AssertEq(plain_sq(5), 25);
    AssertEq(plain_sq(0), 0);
    AssertEq(opt_double(6), 12);
    AssertEq(opt_double(-3), -6);
}

[[cccc::optimize(4)]]
static long opt4_muladd(long a, long b, long c) {
    return a * b + c;
}

[[cccc::test]]
static void test_o4_attr(void) {
    AssertEq(opt4_muladd(3, 4, 5), 17);
    AssertEq(opt4_muladd(0, 100, 7), 7);
}

#pragma cccc suite end
