// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: malformed @template and @inst are rejected.

#include <stddef.h>

@template(T) struct Vec { T *data; size_t len; };
@template(T) T ident(T a) { return a; }
@template(A, B) struct Pair { A a; B b; };

@template(T, int N) struct Arr {
    T v[N];
};
@template(double D) double dbl(void) {
    return D;
}
@template(unsigned char B) int byte_of(void) {
    return B;
}
@template(_BitInt(128) W) int wide(void) {
    return 0;
}

@template(T, int N = 2) struct Dflt {
    T v[N];
};
@template(T, unsigned char N = 300) int big_dflt(void) {
    return N;
}
@template(T, int N = missing_name) int nonconst_dflt(void) {
    return N;
}

enum Odd { n3 = 5, below = -1 };
@template(enum Odd O) int odd(void) {
    return O;
}

#pragma cccc suite begin "templates_errors"

[[cccc::test(error = "takes 2 arguments, 1 given")]]
void test_too_few_arguments(void) {
    @inst(int) Pair p;
}

[[cccc::test(error = "takes 1 argument, 2 given")]]
void test_too_many_arguments(void) {
    @inst(int, int) Vec v;
}

[[cccc::test(error = "'Nope' is not a template")]]
void test_unknown_template(void) {
    @inst(int) Nope v;
}

[[cccc::test(error = "template 'Vec' is a type, not a function")]]
void test_struct_template_as_expression(void) {
    int x = @inst(int) Vec;
}

[[cccc::test(error = "only allowed on a file-scope")]]
void test_template_inside_function(void) {
    @template(T) T inner(T a) { return a; }
}

[[cccc::test(error = "checked-pointer type cannot be a template argument")]]
void test_checked_pointer_argument(void) {
    int * [[cccc::single]] p = 0;
    @inst(typeof(p)) Vec v;
}

[[cccc::test(error = "variable-length array cannot be a template argument")]]
void test_vla_argument(void) {
    int n = 3;
    @inst(int[n]) Vec v;
}

[[cccc::test(error = "must have an integer type")]]
void test_float_constant_parameter(void) {
    @inst(1) dbl();
}

[[cccc::test(error = "must have an integer type")]]
void test_wide_bitint_constant_parameter(void) {
    @inst(1) wide();
}

[[cccc::test(error = "template argument 300 does not fit 'unsigned char'")]]
void test_constant_too_large(void) {
    @inst(300) byte_of();
}

[[cccc::test(error = "template argument -1 does not fit 'unsigned char'")]]
void test_negative_for_unsigned(void) {
    @inst(-1) byte_of();
}

[[cccc::test(error = "not a compile-time constant")]]
void test_non_constant_argument(void) {
    int n = 3;
    @inst(int, n) Arr a;
}

[[cccc::test(error = "expected")]]
void test_type_for_constant_slot(void) {
    @inst(int, int) Arr a;
}

[[cccc::test(error = "expected")]]
void test_constant_for_type_slot(void) {
    @inst(4, 4) Arr a;
}

[[cccc::test(error = "enumerator 'n3' clashes with the number n3")]]
void test_enumerator_clashes_with_negative_number(void) {
    @inst((enum Odd) - 3) odd();
}

#pragma cccc suite end

[[cccc::test(error = "takes 1 to 2 arguments, 0 given")]]
void test_default_too_few(void) {
    @inst Dflt d;
}

[[cccc::test(error = "takes 1 to 2 arguments, 3 given")]]
void test_default_too_many(void) {
    @inst(int, 2, int) Dflt d;
}

[[cccc::test(error = "template argument 300 does not fit 'unsigned char'")]]
void test_default_does_not_fit(void) {
    @inst(int) big_dflt();
}

[[cccc::test(error = "missing_name")]]
void test_default_not_constant(void) {
    @inst(int) nonconst_dflt();
}

[[cccc::test(error = "'Nope' is not a template")]]
void test_postfix_unknown_template(void) {
    Nope @(int) v;
}
