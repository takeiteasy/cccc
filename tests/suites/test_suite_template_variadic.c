// CCCC_FLAGS: --testing
// Suite: template parameter packs (`Ts...`, `int... Ns`) and their expansions.

#include <cccc/derive.h>
#include <stdarg.h>
#include <stddef.h>

@template(Ts...) struct Tuple {
    Ts... v;
};
@specialize() struct Tuple {
    char none;
};
@specialize(short, short) struct Tuple {
    int special;
};

@template(Ts...) struct Ptrs {
    Ts *...p;
};
@template(Ts...) @equals struct Cell {
    Ts... v;
};
@template(T, Ts...) struct Wrap {
    @inst(T, Ts...) Tuple t;
};
@template(T, int... Ns) struct Nd {
    T   data[(Ns * ... * 1)];
    int rank;
};

@template(Ts...) int sum(Ts... xs) {
    return (xs + ... + 0);
}
@template(Ts...) int count(void) {
    return sizeof...(Ts);
}
@template(Ts...) int total_size(void) {
    return (sizeof(Ts) + ...);
}
@template(Ts...) int pick(Ts... xs) {
    return xs...[1];
}
@template(Ts...) Ts...[0] first(Ts... xs) {
    return xs...[0];
}
@template(Ts...) int last_size(void) {
    return sizeof(Ts...[sizeof...(Ts) - 1]);
}
@template(T, Ts...) int lead(T a, Ts... rest) {
    return a * 100 + (rest + ... + 0);
}
@template(Ts...) int tail(Ts... xs, int last) {
    return last + (xs + ... + 0);
}
@template(Ts...) int sizes(void) {
    int a[] = {sizeof(Ts)...};
    return a[0] + a[sizeof...(Ts) - 1];
}
@template(Ts...) int doubled(Ts... xs) {
    return sum @(Ts...)(xs * 2 ...);
}
@template(Ts...) int forward(Ts... xs) {
    return sum @(Ts...)(xs...);
}

@template(Ts...) int fold_left(Ts... xs) {
    return (... - xs);
}
@template(Ts...) int fold_right(Ts... xs) {
    return (xs - ...);
}
@template(Ts...) int fold_left_init(Ts... xs) {
    return (100 - ... - xs);
}
@template(Ts...) int fold_right_init(Ts... xs) {
    return (xs - ... - 100);
}
@template(Ts...) int fold_and(Ts... xs) {
    return (xs && ...);
}
@template(Ts...) int fold_or(Ts... xs) {
    return (... || xs);
}
@template(Ts...) int fold_comma(Ts... xs) {
    int r = 0;
    (void)((r += xs), ...);
    return r;
}

@template(Ts...) int varargs(int n, Ts... xs, ...) {
    va_list ap;
    va_start(ap, xs...[sizeof...(Ts) - 1]);
    int r = n;
    for (int i = 0; i < 2; i++)
        r += va_arg(ap, int);
    va_end(ap);
    return r + (xs + ... + 0);
}

@template(int... Ns) int add_all(void) {
    return (Ns + ... + 0);
}

__template(Ts...) int keyword(Ts... xs) {
    return (xs + ... + 0);
}
[[cccc::template(Ts...)]] int attribute(Ts... xs) {
    return (xs + ... + 0);
}

[[cccc::test]]
void test_member_per_element(void) {
    @inst(int, char) Tuple t = {40, 2};
    AssertTrue(t.v0 + t.v1 == 42);
    AssertTrue(sizeof(t) == sizeof(struct Tuple__int__char));
    AssertTrue(sizeof(@inst(int, char, long) Tuple) ==
               sizeof(struct Tuple__int__char__long));
}

[[cccc::test]]
void test_every_spelling_is_one_instance(void) {
    Tuple @(int, char) a      = {1, 2};
    @inst(int, char) Tuple  b = a;
    struct Tuple__int__char c = b;
    AssertTrue(c.v0 == 1 && c.v1 == 2);
}

[[cccc::test]]
void test_nested_instances_as_arguments(void) {
    Tuple @(Tuple @(int), char) t = {{1}, 2};
    AssertTrue(t.v0.v0 + t.v1 == 3);
}

[[cccc::test]]
void test_pointer_pattern_member(void) {
    int                   i = 5;
    char                  c = 6;
    @inst(int, char) Ptrs p;
    p.p0 = &i;
    p.p1 = &c;
    AssertTrue(*p.p0 + *p.p1 == 11);
}

[[cccc::test]]
void test_derive_attribute_sees_the_members(void) {
    @inst(int, int) Cell a = {1, 2}, b = {1, 2}, c = {1, 3};
    AssertTrue(Cell__int__int_equals(&a, &b));
    AssertFalse(Cell__int__int_equals(&a, &c));
}

[[cccc::test]]
void test_exact_specialization_of_the_empty_pack(void) {
    @inst Tuple e;
    e.none = 7;
    AssertTrue(e.none == 7);
    AssertTrue(sizeof(Tuple @()) == sizeof(char));
}

[[cccc::test]]
void test_exact_specialization_of_a_pack(void) {
    @inst(short, short) Tuple s;
    s.special = 9;
    AssertTrue(s.special == 9);
    @inst(char, int) Tuple other = {1, 2};
    AssertTrue(other.v0 + other.v1 == 3);
}

[[cccc::test]]
void test_fixed_parameter_then_pack(void) {
    @inst(int, char, long) Wrap w = {{1, 2, 3}};
    AssertTrue(w.t.v2 == 3);
    AssertTrue(sizeof(@inst(int) Wrap) == sizeof(struct Tuple__int));
}

[[cccc::test]]
void test_function_pack(void) {
    AssertTrue(sum @(int, long)(40, 2) == 42);
    AssertTrue(sum @()() == 0);
    AssertTrue(@inst(int) sum(5) == 5);
}

[[cccc::test]]
void test_function_instance_is_shared(void) {
    int (*a)(int, long) = sum @(int, long);
    int (*b)(int, long) = sum__int__long;
    AssertTrue(a == b);
}

[[cccc::test]]
void test_sizeof_pack(void) {
    AssertTrue(count @()() == 0);
    AssertTrue(count @(int)() == 1);
    AssertTrue(count @(int, char, long)() == 3);
    AssertTrue(total_size
               @(int, char, long)() == sizeof(int) + 1 + sizeof(long));
}

[[cccc::test]]
void test_pack_index(void) {
    AssertTrue(pick @(int, int)(5, 7) == 7);
    AssertTrue(first @(int, char)(9, 1) == 9);
    AssertTrue(last_size @(char, int, long)() == sizeof(long));
}

[[cccc::test]]
void test_pattern_expansion(void) {
    AssertTrue(doubled @(int, int)(3, 4) == 14);
    AssertTrue(forward @(int, int, int)(1, 2, 3) == 6);
    AssertTrue(sizes @(char, int, long)() == 1 + sizeof(long));
}

[[cccc::test]]
void test_empty_pack_drops_its_comma(void) {
    AssertTrue(lead @(int)(5) == 500);
    AssertTrue(lead @(int, int, int)(2, 3, 4) == 207);
    AssertTrue(tail @()(7) == 7);
    AssertTrue(tail @(int, int)(1, 2, 3) == 6);
}

[[cccc::test]]
void test_unary_folds(void) {
    AssertTrue(fold_left @(int, int, int)(10, 3, 2) == 5);
    AssertTrue(fold_right @(int, int, int)(10, 3, 2) == 9);
    AssertTrue(fold_left @(int)(10) == 10);
    AssertTrue(fold_comma @(int, int, int)(1, 2, 3) == 6);
}

[[cccc::test]]
void test_binary_folds(void) {
    AssertTrue(fold_left_init @(int, int)(1, 2) == 97);
    AssertTrue(fold_right_init @(int, int)(1, 2) == 99);
    AssertTrue(fold_left_init @()() == 100);
}

[[cccc::test]]
void test_empty_unary_folds(void) {
    AssertTrue(fold_and @()() == 1);
    AssertTrue(fold_or @()() == 0);
    AssertTrue(fold_and @(int, int)(1, 0) == 0);
    AssertTrue(fold_or @(int, int)(0, 5) == 1);
}

[[cccc::test]]
void test_c_varargs_after_a_pack(void) {
    AssertTrue(varargs @(int, int)(1, 10, 20, 3, 4) == 38);
}

[[cccc::test]]
void test_constant_pack(void) {
    AssertTrue(add_all @(1, 2, 3)() == 6);
    AssertTrue(add_all @()() == 0);
    @inst(int, 2, 3, 4) Nd nd;
    AssertTrue(sizeof(nd.data) == 2 * 3 * 4 * sizeof(int));
    AssertTrue(sizeof(((@inst(char) Nd *)0)->data) == 1);
}

[[cccc::test]]
void test_more_than_sixteen_arguments(void) {
    AssertTrue(add_all @(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                         17, 18, 19, 20)() == 210);
    AssertTrue(count @(int, int, int, int, int, int, int, int, int, int, int,
                       int, int, int, int, int, int, int)() == 18);
}

[[cccc::test]]
void test_marker_spellings(void) {
    AssertTrue(keyword @(int, int)(1, 2) == 3);
    AssertTrue(attribute @(int, int)(3, 4) == 7);
    AssertTrue(__inst(int, int) keyword(5, 6) == 11);
}
