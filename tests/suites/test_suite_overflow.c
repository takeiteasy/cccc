// CCCC_FLAGS: --testing
// Consolidated suite: integer overflow detection, div-by-zero, invalid call
// Source tests: test_overflow_{add,sub,mul,div,div_signed,opt_add,none},
//               test_dlfcn_invalid_call, test_optimizer_trap_preservation
// Notes:
//  - test_ffi_fatal_error and test_ffi_type_check_arity use --ffi-deny= /
//    --ffi-type-checking which cannot be per-test flags; those stay external.
//  - test_stack_overflow_large_frame exits 42 (VM heap holds the 4 MB array);
//    it was passing via exit 42, not the EXPECT_RUNTIME_ERROR path — migrate
//    it as a normal behavioral test instead.

#include "limits.h"
#include <stdint.h>

#pragma cccc suite begin "overflow"

// ── test_overflow_none: normal arithmetic is safe with --overflow-checks ──
[[cccc::test(flags = "--overflow-checks")]]
void test_overflow_checks_clean(void) {
    int a = 10 + 20;
    int b = 50 - 20;
    int c = 6 * 7;
    int d = 84 / 2;
    AssertEq(a, 30);
    AssertEq(b, 30);
    AssertEq(c, 42);
    AssertEq(d, 42);
}

// ── test_overflow_add: LLONG_MAX + 1 must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_add(void) {
    long long x = LLONG_MAX;
    long long result = x + 1;
    (void)result;
}

// ── test_overflow_sub: LLONG_MIN - 1 must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_sub(void) {
    long long x = LLONG_MIN;
    long long result = x - 1;
    (void)result;
}

// ── test_overflow_mul: LLONG_MAX * 2 must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_mul(void) {
    long long x = LLONG_MAX;
    long long result = x * 2;
    (void)result;
}

// ── test_overflow_div: division by zero must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_div_by_zero(void) {
    int x = 42;
    int y = 0;
    int result = x / y;
    (void)result;
}

// ── test_overflow_div_signed: LLONG_MIN / -1 must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_div_signed(void) {
    long long x = LLONG_MIN;
    long long result = x / -1;
    (void)result;
}

// ── test_overflow_opt_add: overflow traps survive -O3 constant folding ──
[[cccc::test(flags = "--overflow-checks --optimize=3", exit_code = 255)]]
void test_overflow_opt_add(void) {
    long long result = LLONG_MAX + 1LL;
    (void)result;
}

// ── test_optimizer_trap_preservation: div-by-zero trap survives -O3 ──
[[cccc::test(flags = "--optimize=3", exit_code = 255)]]
void test_div_by_zero_optimized(void) {
    int result = 42 / (6 - 6);
    (void)result;
}

// ── test_dlfcn_invalid_call: calling invalid function pointer exits 255 ──
[[cccc::test(exit_code = 255)]]
void test_invalid_funcptr(void) {
    int (*fn)(void) = (int (*)(void))(intptr_t)12345;
    fn();
}


#pragma cccc suite end
