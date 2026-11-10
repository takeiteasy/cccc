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

#pragma cccc suite begin "templates_errors"

[[cccc::test(error = "takes 2 arguments, 1 given")]]
void test_too_few_arguments(void) {
    @inst(Pair, int) p;
}

[[cccc::test(error = "takes 1 argument, 2 given")]]
void test_too_many_arguments(void) {
    @inst(Vec, int, int) v;
}

[[cccc::test(error = "'Nope' is not a template")]]
void test_unknown_template(void) {
    @inst(Nope, int) v;
}

[[cccc::test(error = "template 'Vec' is a type, not a function")]]
void test_struct_template_as_expression(void) {
    int x = @inst(Vec, int);
}

[[cccc::test(error = "only allowed on a file-scope")]]
void test_template_inside_function(void) {
    @template(T) T inner(T a) { return a; }
}

[[cccc::test(error = "checked-pointer type cannot be a template argument")]]
void test_checked_pointer_argument(void) {
    int * [[cccc::single]] p = 0;
    @inst(Vec, typeof(p)) v;
}

[[cccc::test(error = "variable-length array cannot be a template argument")]]
void test_vla_argument(void) {
    int n = 3;
    @inst(Vec, int[n]) v;
}

[[cccc::test(error = "must have an integer type")]]
void test_float_constant_parameter(void) {
    @inst(dbl, 1)();
}

[[cccc::test(error = "must have an integer type")]]
void test_wide_bitint_constant_parameter(void) {
    @inst(wide, 1)();
}

[[cccc::test(error = "template argument 300 does not fit 'unsigned char'")]]
void test_constant_too_large(void) {
    @inst(byte_of, 300)();
}

[[cccc::test(error = "template argument -1 does not fit 'unsigned char'")]]
void test_negative_for_unsigned(void) {
    @inst(byte_of, -1)();
}

[[cccc::test(error = "not a compile-time constant")]]
void test_non_constant_argument(void) {
    int n = 3;
    @inst(Arr, int, n) a;
}

[[cccc::test(error = "expected")]]
void test_type_for_constant_slot(void) {
    @inst(Arr, int, int) a;
}

[[cccc::test(error = "expected")]]
void test_constant_for_type_slot(void) {
    @inst(Arr, 4, 4) a;
}

#pragma cccc suite end
