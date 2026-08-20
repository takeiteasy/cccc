// CCCC_FLAGS: --testing
// Consolidated suite: compile-error tests (parse-time, inside function bodies).
// Ticket #615.
#include <complex.h>

#pragma cccc suite begin "compile_errors"

// --- Bitfields ---

[[cccc::test(expect_compile_error = true)]]
void test_bitfield_negative_width(void) {
    struct S {
        int x : -1;
    };
    (void)(struct S){0};
}

[[cccc::test(expect_compile_error = true)]]
void test_bitfield_too_wide(void) {
    struct S {
        unsigned char x : 9;
    };
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
    x           = 10;
}

[[cccc::test(expect_compile_error = true)]]
void test_const_pointer(void) {
    int        v = 10;
    const int *p = &v;
    *p           = 20;
}

[[cccc::test(expect_compile_error = true)]]
void test_const_ptr(void) {
    int        x = 10, y = 20;
    int *const p = &x;
    p            = &y;
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

// --- Parser errors with specific stderr ---

// [from test_parser_invalid_operands_error]
[[cccc::test(expect_compile_error = true, error = "cannot add two pointers")]]
void test_parser_invalid_operands_error(void) {
    int *p = 0;
    (void)(p + p);
}

// [from test_parser_no_such_member_error]
struct PErr_Foo {
    int x;
};
[[cccc::test(expect_compile_error = true, error = "no such member")]]
void test_parser_no_such_member_error(void) {
    struct PErr_Foo f;
    (void)(f.bar);
}

// [from test_parser_undefined_variable_error]
[[cccc::test(expect_compile_error = true, error = "undefined variable")]]
void test_parser_undefined_variable_error(void) {
    (void)(undeclared_xyz);
}

// --- C23 constexpr in function body ---

// [from test_constexpr_nonconstant_init_error]
[[cccc::test(expect_compile_error = true,
             error = "constexpr initializer is not a constant expression")]]
void test_constexpr_nonconstant_init_error(void) {
    int           x = 1;
    constexpr int n = x;
    (void)n;
}

// --- C23 empty params (C23 default: int f() means f(void)) ---

// [from test_c23_empty_params_error]
[[cccc::test(expect_compile_error = true, error = "too many arguments")]]
void test_c23_empty_params_error(void) {
    int add_ep();
    (void)add_ep(1, 2);
}

// --- C23 compound literal storage class errors (inside function body) ---

// [from test_c23_compound_literal_auto_error]
[[cccc::test(expect_compile_error = true)]]
void test_c23_compound_literal_auto_error(void) {
    int *p = &(auto int){7};
    (void)p;
}

// [from test_c23_compound_literal_extern_error]
[[cccc::test(expect_compile_error = true)]]
void test_c23_compound_literal_extern_error(void) {
    int *p = &(extern int){7};
    (void)p;
}

// [from test_c23_compound_literal_inline_error]
[[cccc::test(expect_compile_error = true)]]
void test_c23_compound_literal_inline_error(void) {
    int *p = &(inline int){7};
    (void)p;
}

// [from test_c23_compound_literal_typedef_error]
[[cccc::test(expect_compile_error = true)]]
void test_c23_compound_literal_typedef_error(void) {
    int *p = &(typedef int){7};
    (void)p;
}

// --- C23 duplicate/incompatible type redeclarations ---

// [from test_c23_duplicate_enum_constant_error]
[[cccc::test(expect_compile_error = true)]]
void test_c23_duplicate_enum_constant_error(void) {
    enum DupFirst { DUP_CONST_A = 1 };
    enum DupSecond { DUP_CONST_A = 2 };
    (void)(DUP_CONST_A);
}

// [from test_c23_incompatible_enum_redecl_error]
[[cccc::test(expect_compile_error = true)]]
void test_c23_incompatible_enum_redecl_error(void) {
    enum IncompBadEnum { INCOMPAT_ENUM_A = 1 };
    enum IncompBadEnum { INCOMPAT_ENUM_A = 2 };
    (void)(INCOMPAT_ENUM_A);
}

// [from test_c23_incompatible_struct_redecl_error]
[[cccc::test(expect_compile_error = true)]]
void test_c23_incompatible_struct_redecl_error(void) {
    struct IncompBadStruct {
        int x;
    };
    struct IncompBadStruct {
        long x;
    };
    struct IncompBadStruct s;
    (void)s;
}

// [from test_c23_incompatible_union_redecl_error]
[[cccc::test(expect_compile_error = true)]]
void test_c23_incompatible_union_redecl_error(void) {
    union IncompBadUnion {
        int x;
    };
    union IncompBadUnion {
        long x;
    };
    union IncompBadUnion u;
    (void)u;
}

// --- constexpr error tests ---

// [from test_constexpr_missing_init_error]
[[cccc::test(expect_compile_error = true,
             error = "constexpr object requires an initializer")]]
void test_constexpr_missing_init_error(void) {
    constexpr int ce_no_init;
    (void)ce_no_init;
}

// [from test_constexpr_restrict_error]
[[cccc::test(expect_compile_error = true,
             error = "constexpr object has unsupported type or qualifiers")]]
void test_constexpr_restrict_error(void) {
    int ce_target;
    constexpr int *restrict ce_p = &ce_target;
    (void)ce_p;
}

// [from test_constexpr_volatile_error]
[[cccc::test(expect_compile_error = true,
             error = "constexpr object has unsupported type or qualifiers")]]
void test_constexpr_volatile_error(void) {
    constexpr volatile int ce_vol_n = 1;
    (void)ce_vol_n;
}

// --- static_assert error ---

// [from test_static_assert_no_message_error]
[[cccc::test(expect_compile_error = true, error = "static assertion failed")]]
void test_static_assert_no_message_error(void) {
    static_assert(0);
}

#pragma cccc suite end
