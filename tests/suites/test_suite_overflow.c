// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: div-by-zero and invalid indirect-call traps are VM-only (native.md divergences); the --overflow-checks tests are flags= deltas
// Consolidated suite: integer overflow detection, div-by-zero, invalid call
// Source tests: test_overflow_{add,sub,mul,div,div_signed,opt_add,none},
//               test_dlfcn_invalid_call, test_optimizer_trap_preservation
// Notes:
//  - test_ffi_fatal_error and test_ffi_type_check_arity use --ffi-deny= /
//    --ffi-type-checking which cannot be per-test flags; those stay external.
//  - test_stack_overflow_large_frame exits 42 (VM heap holds the 4 MB array);
//    it was passing via exit 42, not via a runtime-error expectation —
//    migrate it as a normal behavioral test instead.

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
    long long x      = LLONG_MAX;
    long long result = x + 1;
    (void)result;
}

// ── test_overflow_sub: LLONG_MIN - 1 must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_sub(void) {
    long long x      = LLONG_MIN;
    long long result = x - 1;
    (void)result;
}

// ── test_overflow_mul: LLONG_MAX * 2 must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_mul(void) {
    long long x      = LLONG_MAX;
    long long result = x * 2;
    (void)result;
}

// ── test_overflow_div: division by zero must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_div_by_zero(void) {
    int x      = 42;
    int y      = 0;
    int result = x / y;
    (void)result;
}

// ── test_overflow_div_signed: LLONG_MIN / -1 must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_div_signed(void) {
    long long x      = LLONG_MIN;
    long long result = x / -1;
    (void)result;
}

// ── test_overflow_opt_add: overflow traps survive -O3 constant folding ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_opt_add(void) {
    long long result = LLONG_MAX + 1LL;
    (void)result;
}

// ── 32-bit int: INT_MAX + 1 must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_int_add(void) {
    volatile int x      = INT_MAX;
    int          result = x + 1;
    (void)result;
}

// ── 32-bit int: INT_MIN - 1 must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_int_sub(void) {
    volatile int x      = INT_MIN;
    int          result = x - 1;
    (void)result;
}

// ── 32-bit int: 65536 * 65536 must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_int_mul(void) {
    volatile int x      = 65536;
    int          result = x * x;
    (void)result;
}

// ── 32-bit int: INT_MIN / -1 must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_int_div(void) {
    volatile int x      = INT_MIN;
    int          result = x / -1;
    (void)result;
}

// ── 32-bit int: -INT_MIN must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_int_neg(void) {
    volatile int x      = INT_MIN;
    int          result = -x;
    (void)result;
}

// ── 64-bit: -LLONG_MIN must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_llong_neg(void) {
    volatile long long x      = LLONG_MIN;
    long long          result = -x;
    (void)result;
}

// ── compound assignment: INT_MAX += 1 must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_int_compound_add(void) {
    volatile int x  = INT_MAX;
    x              += 1;
}

// ── increment: INT_MAX++ must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_int_increment(void) {
    volatile int x = INT_MAX;
    x++;
}

// ── signed _BitInt(8): 127 + 1 must abort ──
[[cccc::test(flags = "--overflow-checks", exit_code = 255)]]
void test_overflow_bitint_add(void) {
    volatile _BitInt(8) x = 127;
    _BitInt(8) result     = x + (_BitInt(8))1;
    (void)result;
}

// ── in-range results and non-signed types do not trap ──
[[cccc::test(flags = "--overflow-checks")]]
void test_overflow_checks_no_false_trap(void) {
    volatile int x = INT_MAX;
    AssertEq(x - 1 + 1, INT_MAX);
    AssertEq(-x, -INT_MAX);
    volatile unsigned u = UINT_MAX;
    AssertEq(u + 1u, 0u);
    volatile char c  = 127;
    c               += 1;
    AssertEq((int)c, -128);
    volatile _BitInt(8) b = 126;
    AssertEq((int)(b + (_BitInt(8))1), 127);
    volatile long long big = 3000000000LL;
    AssertEq(big * 3, 9000000000LL);
}

// ── test_optimizer_trap_preservation: div-by-zero trap survives -O3 ──
[[cccc::test(exit_code = 255)]]
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
