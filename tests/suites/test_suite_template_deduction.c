// CCCC_FLAGS: --testing
// Suite: a function template called without @inst deduces its arguments from
// the argument types.
#include <stddef.h>

@template(T) struct Vec {
    T x, y;
};
@template(T, int N) struct Arr {
    T v[N];
};

@template(T) T maxof(T a, T b) {
    return a > b ? a : b;
}
@template(T) T sum_vec(@inst(T) Vec v) {
    return v.x + v.y;
}
@template(T, int N) T sum_arr(@inst(T, N) Arr a) {
    T s = 0;
    for (int i = 0; i < N; i++)
        s += a.v[i];
    return s;
}
@template(T, int N) int rows_of(T (*p)[N]) {
    return N;
}
@template(T) T apply(T (*f)(T), T x) {
    return f(x);
}
@template(T) T first(const T *p, size_t n) {
    return n ? p[0] : 0;
}
@template(T) T total(T *p, size_t n) {
    T s = 0;
    for (size_t i = 0; i < n; i++)
        s += p[i];
    return s;
}
@template(T, U = T) T add_to(T a, U b) {
    return a + (T)b;
}
@template(T, U) U widen(T a, U b) {
    return (U)a + b;
}
@template(T) int is_set(T *p) {
    return p != NULL;
}
@template(T) T twice(T a) {
    return maxof(a, a) * 2;
}
@template(T) T ident(T a) {
    return a;
}
@template(T) int bump(T a) {
    static int n;
    return ++n;
}

static int doubled(int x) {
    return x * 2;
}

[[cccc::test]]
void test_scalar_arguments(void) {
    AssertEq(maxof(40, 42), 42);
    AssertEq(maxof('a', 'b'), 'b');
    AssertEq(maxof(1L, 2L), 2L);
    AssertEq(maxof(1.5, 0.5), 1.5);
}

[[cccc::test]]
void test_same_instance_as_inst(void) {
    AssertEq(bump(1), 1);
    AssertEq(@inst(int) bump(1), 2);
    AssertEq(bump(1), 3);
    AssertEq(bump(1L), 1);
}

[[cccc::test]]
void test_instance_pattern(void) {
    @inst(int) Vec v = {20, 22};
    AssertEq(sum_vec(v), 42);
    @inst(double) Vec d = {1.5, 2.5};
    AssertEq(sum_vec(d), 4.0);
}

[[cccc::test]]
void test_constant_through_instance(void) {
    @inst(int, 3) Arr a = {{1, 2, 3}};
    AssertEq(sum_arr(a), 6);
}

[[cccc::test]]
void test_constant_through_pointer_to_array(void) {
    int a[4] = {1, 2, 3, 4};
    AssertEq(rows_of(&a), 4);
}

[[cccc::test]]
void test_function_pointer_pattern(void) {
    AssertEq(apply(doubled, 21), 42);
}

[[cccc::test]]
void test_array_decays(void) {
    int a[4] = {1, 2, 3, 4};
    AssertEq(total(a, 4), 10);
    AssertEq(total(a, 2), 3);
}

[[cccc::test]]
void test_const_pointer_parameter_takes_plain_pointer(void) {
    int        a[3] = {7, 8, 9};
    int       *p    = a;
    const int *c    = a;
    AssertEq(first(a, 3), 7);
    AssertEq(first(p, 3), 7);
    AssertEq(first(c, 3), 7);
}

[[cccc::test]]
void test_qualified_arguments_deduce_unqualified(void) {
    const int    c = 5;
    volatile int v = 2;
    AssertEq(ident(c) + ident(v), 7);
}

[[cccc::test]]
void test_string_literal_is_char_pointer(void) {
    AssertEq(is_set("hi"), 1);
}

[[cccc::test]]
void test_default_deduced_or_not(void) {
    AssertEq(add_to(40, 2), 42);
    AssertEq(add_to(40, 2.5), 42);
}

[[cccc::test]]
void test_partial_explicit_arguments(void) {
    AssertEq(@inst(int) widen(1, 2.0), 3.0);
    AssertEq(@inst(long) widen(1, 2), 3);
}

[[cccc::test]]
void test_explicit_parameter_takes_conversions(void) {
    AssertEq(@inst(double) maxof(1, 2.5), 2.5);
}

[[cccc::test]]
void test_deduced_call_inside_a_template(void) {
    AssertEq(twice(21), 42);
}

[[cccc::test]]
void test_local_name_wins(void) {
    int maxof = 7;
    AssertEq(maxof, 7);
}

[[cccc::test]]
void test_call_result_is_postfix_operand(void) {
    @inst(int) Vec v = {1, 2};
    AssertEq(ident(v).y, 2);
}
