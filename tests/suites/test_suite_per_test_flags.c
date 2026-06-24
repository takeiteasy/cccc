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

#pragma cccc suite end
