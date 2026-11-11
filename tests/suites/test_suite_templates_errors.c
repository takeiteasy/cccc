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
@template(long double L) long double ldbl(void) {
    return L;
}
@template(double D) double dbl(void) {
    return D;
}
@template(float F) float flt(void) {
    return F;
}
@template(int (*P)(int)) int fnptr(void) {
    return P != NULL;
}
@template(const int *Q) int objptr(void) {
    return *Q;
}
@template(struct Odd2 { int a; } S) int aggregate_param(void) {
    return 0;
}
@template(int (*P)(int)) int fnptr_a(void) {
    return 1;
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

[[cccc::test(error = "cannot have type long double")]]
void test_long_double_constant_parameter(void) {
    @inst(1) ldbl();
}

[[cccc::test(error = "NaN is not a valid template argument")]]
void test_nan_argument(void) {
    @inst(0.0 / 0.0) dbl();
}

[[cccc::test(error = "template argument 1e+40 does not fit 'float'")]]
void test_float_argument_overflows(void) {
    @inst(1e40) flt();
}

[[cccc::test(error = "must be a real number")]]
void test_float_argument_not_a_number(void) {
    @inst(&test_nan_argument) dbl();
}

[[cccc::test(error = "must have an integer, float, double or pointer type")]]
void test_aggregate_constant_parameter(void) {
    @inst(1) aggregate_param();
}

static int null(int x) {
    return x;
}
static int fn_one(int x) {
    return x;
}
static int fn_other(void) {
    return 0;
}
static int global_int = 3;
static int global_arr[4];

[[cccc::test(error = "a string literal cannot be a template argument")]]
void test_string_literal_argument(void) {
    @inst("abc") objptr();
}

[[cccc::test(error = "must be a function or an object with file-scope or "
                     "static storage")]]
void test_local_address_argument(void) {
    int local = 1;
    @inst(&local) objptr();
}

[[cccc::test(error = "must be a function or an object with file-scope or "
                     "static storage")]]
void test_block_static_argument(void) {
    static int inner = 1;
    @inst(&inner) objptr();
}

[[cccc::test(error = "must name a function or the address of a named object")]]
void test_element_address_argument(void) {
    @inst(&global_arr[2]) objptr();
}

[[cccc::test(error = "must be a function, the address of an object, or null")]]
void test_nonzero_integer_pointer_argument(void) {
    @inst(5) fnptr();
}

[[cccc::test(error = "does not match the parameter's pointer type")]]
void test_incompatible_function_argument(void) {
    @inst(fn_other) fnptr();
}

[[cccc::test(error = "is an object but the template parameter points to a "
                     "function")]]
void test_object_for_function_pointer(void) {
    @inst(&global_int) fnptr();
}

[[cccc::test(error = "is a function but the template parameter points to an "
                     "object")]]
void test_function_for_object_pointer(void) {
    @inst(fn_one) objptr();
}

[[cccc::test(error = "is also the name of a different instance")]]
void test_null_clashes_with_function_named_null(void) {
    @inst(null) fnptr_a();
    @inst(0) fnptr_a();
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
