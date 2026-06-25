// CCCC_FLAGS: --testing
// Migration of legacy EXPECT_COMPILE_ERROR tests that have parse-time errors
// (no flags, no stderr pattern required). Ticket #615.
#include <complex.h>

#pragma cccc suite begin "compile_errors"

// --- Bitfields ---

[[cccc::test(expect_compile_error = true)]]
void test_bitfield_negative_width(void) {
    struct S { int x : -1; };
    (void)(struct S){0};
}

[[cccc::test(expect_compile_error = true)]]
void test_bitfield_too_wide(void) {
    struct S { unsigned char x : 9; };
    (void)(struct S){0};
}

// --- C23 auto ---

[[cccc::test(expect_compile_error = true)]]
void test_c23_auto_bad_ptr(void) {
    auto *p = 5;
    (void)p;
}

[[cccc::test(expect_compile_error = true)]]
void test_c23_auto_combined_type(void) {
    auto int x = 5;
    (void)x;
}

[[cccc::test(expect_compile_error = true)]]
void test_c23_auto_no_init(void) {
    auto x;
    (void)x;
}

// --- Complex comparison order ---

[[cccc::test(expect_compile_error = true)]]
void test_complex_comparison_order(void) {
    double complex z = CMPLX(1.0, 2.0);
    (void)(z < CMPLX(2.0, 3.0));
}

// --- Const violations ---

[[cccc::test(expect_compile_error = true)]]
void test_const_assign(void) {
    const int x = 42;
    x = 10;
}

[[cccc::test(expect_compile_error = true)]]
void test_const_pointer(void) {
    int v = 10;
    const int *p = &v;
    *p = 20;
}

[[cccc::test(expect_compile_error = true)]]
void test_const_ptr(void) {
    int x = 10, y = 20;
    int *const p = &x;
    p = &y;
    (void)p;
}

// --- Constexpr divide/mod by zero ---

[[cccc::test(expect_compile_error = true)]]
void test_constexpr_div_zero(void) {
    int a[1 / 0];
    (void)a;
}

[[cccc::test(expect_compile_error = true)]]
void test_constexpr_mod_zero(void) {
    enum { A = 1 % 0 };
    (void)A;
}

// --- Enum errors ---

[[cccc::test(expect_compile_error = true)]]
void test_enum_fwd_no_underlying(void) {
    enum NoBase;
}

[[cccc::test(expect_compile_error = true)]]
void test_enum_underlying_bool(void) {
    enum E : _Bool { EA = 0, EB = 1 };
    (void)EA;
}

// --- Undefined variables ---

[[cccc::test(expect_compile_error = true)]]
void test_undefined_var_assign(void) {
    undefined_var = 10;
}

[[cccc::test(expect_compile_error = true)]]
void test_undefined_result_assign(void) {
    result = 42;
}

[[cccc::test(expect_compile_error = true)]]
void test_undefined_x_assign(void) {
    x = 5;
}

// --- Fuzz regression: invalid pointer arithmetic (#143) ---

[[cccc::test(expect_compile_error = true)]]
void test_fuzz_regr_invalid_arith(void) {
    int b = 10 - &;
    (void)b;
}

#pragma cccc suite end
