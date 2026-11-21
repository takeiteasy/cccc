// CCCC_FLAGS: --testing
// Suite: a function template's parameter pack is deduced from the call's
// arguments, one type per argument.
#include <stddef.h>

@template(T) struct Vec {
    T x, y;
};

@template(Ts...) int sum(Ts... xs) {
    return (xs + ... + 0);
}
@template(Ts...) int count(Ts... xs) {
    return sizeof...(Ts);
}
@template(T, Ts...) int lead(T a, Ts... rest) {
    return a * 100 + (rest + ... + 0);
}
@template(Ts...) int tail(Ts... xs, int last) {
    return last + (xs + ... + 0);
}
@template(T, Ts...) int between(T first, Ts... xs, T last) {
    return first * 100 + last + (xs + ... + 0);
}
@template(Ts...) int derefs(Ts *...ps) {
    return (*ps + ... + 0);
}
@template(Ts...) int sizes(const Ts *...ps) {
    return (sizeof(Ts) + ...);
}
@template(Ts...) int vec_sizes(@inst(Ts) Vec... vs) {
    return sizeof...(Ts) * 100 + (sizeof(Ts) + ...);
}
@template(Ts...) int forward(Ts... xs) {
    return sum(xs...);
}
@template(Ts...) size_t bytes(Ts... xs) {
    return (sizeof(xs) + ... + 0);
}
@template(Ts...) int elem_sizes(Ts... xs) {
    return (sizeof(Ts) + ...);
}
@template(T, Ts...) int lead_sizes(T a, Ts... rest) {
    return (sizeof(Ts) + ... + 0);
}
@template(Ts...) int tail_sizes(Ts... xs, int last) {
    return (sizeof(Ts) + ... + 0) + last;
}
@template(Ts...) int pack_only_in_args(void) {
    return sizeof...(Ts);
}
@template(Ts...) int varargs_after(int n, Ts... xs, ...) {
    return n + sizeof...(Ts);
}
@template(Ts...) Ts...[0] first(Ts... xs) {
    return xs...[0];
}
@template(T) T (*identity_fn(T x))(T) {
    return NULL;
}

[[cccc::test]]
void test_pack_takes_every_argument(void) {
    AssertEq(sum(40, 2), 42);
    AssertEq(sum(1, 2, 3, 4), 10);
    AssertEq(count(1, 'a', 2.0, 3L), 4);
}

[[cccc::test]]
void test_empty_pack(void) {
    AssertEq(sum(), 0);
    AssertEq(count(), 0);
}

[[cccc::test]]
void test_empty_instance_does_not_hide_the_template(void) {
    AssertEq(count(), 0);
    AssertEq(count(1, 2), 2);
    AssertEq(count(), 0);
}

[[cccc::test]]
void test_deduced_instance_is_the_inst_instance(void) {
    int (*a)(int, long) = sum<int, long>;
    sum(1, 2L);
    AssertTrue((void *)a == (void *)sum_int_long);
}

[[cccc::test]]
void test_each_argument_has_its_own_type(void) {
    AssertEq(bytes(1, 'a', 2.0), sizeof(int) * 2 + sizeof(double));
    AssertEq(bytes((char)1, (short)2), 3);
}

[[cccc::test]]
void test_fixed_parameter_then_pack(void) {
    AssertEq(lead(5, 1, 2), 503);
    AssertEq(lead(5), 500);
}

[[cccc::test]]
void test_fixed_parameter_after_pack(void) {
    AssertEq(tail(1, 2, 3), 6);
    AssertEq(tail(7), 7);
    AssertEq(between(1, 2, 3, 4), 109);
}

[[cccc::test]]
void test_element_pattern(void) {
    int  a      = 5;
    char b      = 6;
    int  arr[2] = {1, 2};
    AssertEq(derefs(&a, &b, arr), 12);
    AssertEq(derefs(&a), 5);
    AssertEq(sizes(&a, &b), sizeof(int) + 1);
}

[[cccc::test]]
void test_element_pattern_with_an_instance(void) {
    @inst(int) Vec  u = {1, 2};
    @inst(char) Vec v = {3, 4};
    AssertEq(vec_sizes(u, v), 200 + (int)sizeof(int) + 1);
}

[[cccc::test]]
void test_qualifiers_are_dropped(void) {
    const int    c = 40;
    volatile int v = 2;
    AssertEq(sum(c, v), 42);
    AssertTrue((void *)sum_int_int != NULL);
}

[[cccc::test]]
void test_more_than_sixteen_arguments(void) {
    AssertEq(count(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
                   18, 19, 20),
             20);
    AssertEq(sum(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                 19, 20),
             210);
}

[[cccc::test]]
void test_deduced_call_inside_a_template(void) {
    AssertEq(forward(40, 2), 42);
}

[[cccc::test]]
void test_return_type_names_the_pack(void) {
    AssertEq(first(42, 2.0), 42);
    AssertEq(first(2.5, 1), 2.5);
    AssertTrue((void *)first_int_double != NULL);
}

[[cccc::test]]
void test_function_pointer_return_type(void) {
    AssertTrue(identity_fn(1) == NULL);
}

[[cccc::test]]
void test_inst_names_leading_pack_elements(void) {
    AssertEq(@inst(int) sum(5, 2.0), 7);
    AssertEq(@inst(char) elem_sizes(1, 2L), 1 + (int)sizeof(long));
    AssertEq(@inst(double) elem_sizes(1, 2),
             (int)sizeof(double) + (int)sizeof(int));
    AssertTrue((void *)sum_int_double != NULL);
}

[[cccc::test]]
void test_inst_naming_the_whole_pack_is_the_same_instance(void) {
    AssertEq(@inst(int) sum(5), 5);
    AssertEq(@inst(int, long) count(1, 2), 2);
    AssertEq(@inst(int, int) sum(40, 2), 42);
    AssertEq(@inst(int, long) count(1, 2, 3, 4), 4);
}

[[cccc::test]]
void test_inst_names_fixed_parameter_then_pack_elements(void) {
    AssertEq(@inst(int) lead(5, 1, 2), 503);
    AssertEq(@inst(int) lead_sizes(5, 1, 2L),
             (int)sizeof(int) + (int)sizeof(long));
    AssertEq(@inst(int, char) lead_sizes(5, 1, 2L), 1 + (int)sizeof(long));
    AssertEq(@inst(int, char, short) lead_sizes(5, 1, 2L), 1 + 2);
}

[[cccc::test]]
void test_inst_names_elements_before_fixed_parameters(void) {
    AssertEq(@inst(long) tail_sizes(1, 2, 3),
             (int)sizeof(long) + (int)sizeof(int) + 3);
    AssertEq(@inst(long) tail(1, 2, 3), 6);
}

[[cccc::test]]
void test_inst_without_a_pack_parameter_is_the_whole_list(void) {
    AssertEq(@inst(int, char) pack_only_in_args(), 2);
    AssertEq(@inst(int) varargs_after(1, 2), 2);
}
