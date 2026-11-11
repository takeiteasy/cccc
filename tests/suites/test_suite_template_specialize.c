// CCCC_FLAGS: --testing
// Suite: @specialize replaces a template's body for one argument list.

#include <cccc/derive.h>
#include <stddef.h>
#include <stdint.h>

@template(T) struct Vec {
    T     *data;
    size_t len;
};
@specialize(bool) struct Vec {
    uint64_t bits;
    size_t   len;
};

@template(T) union Bits {
    T             value;
    unsigned char raw[sizeof(T)];
};
@specialize(char) union Bits {
    char value;
    int  wide;
};

@template(T) T pick(T a, T b) {
    return a > b ? a : b;
}
@specialize(bool) bool pick(bool a, bool b) {
    return a || b;
}

@template(T) T count_down(T n) {
    return n == 0 ? 0 : count_down(n - 1);
}
@specialize(long) long count_down(long n) {
    return n == 0 ? 100 : count_down(n - 1);
}

@template(T) enum Level : T { LOW, HIGH = LOW + 5 };
@specialize(char) enum Level : char { LOW = 1, HIGH = 9 };

@template(T) typedef T *Ptr;
@specialize(bool) typedef uint64_t Ptr;

@template(T, int N = 8) struct Arr {
    T v[N];
};
@specialize(int, 4) struct Arr {
    int v[2];
};
@specialize(char) struct Arr {
    char tag;
};

@template(T) @equals struct Cell {
    T a;
    T b;
};
@specialize(int) @equals struct Cell {
    int a;
    int b;
    int c;
};

@template(T) struct Wrap {
    @inst(T) Vec inner;
};

@template(T) struct Plain {
    T x;
};
@specialize(long) struct Plain {
    long x;
    long y;
};

@template(T) T unused(T v) {
    return v;
}
@specialize(short) short unused(short v) {
    return v + undeclared_name;
}

[[cccc::test]]
void test_struct_specialization(void) {
    @inst(bool) Vec v = {5, 2};
    @inst(int) Vec  p = {0, 1};
    AssertTrue(sizeof(@inst(bool) Vec) == sizeof(uint64_t) + sizeof(size_t));
    AssertTrue(sizeof(@inst(int) Vec) == sizeof(int *) + sizeof(size_t));
    AssertTrue(v.bits == 5 && v.len == 2 && p.len == 1);
}

[[cccc::test]]
void test_every_spelling_selects_it(void) {
    Vec @(bool) a      = {1, 1};
    @inst(bool) Vec  b = {2, 2};
    struct Vec__bool c = {3, 3};
    AssertTrue(a.bits == 1 && b.bits == 2 && c.bits == 3);
}

[[cccc::test]]
void test_alias_names_same_specialization(void) {
    typedef _Bool   flag;
    @inst(flag) Vec v = {4, 1};
    AssertTrue(v.bits == 4);
}

[[cccc::test]]
void test_union_specialization(void) {
    @inst(char) Bits b;
    AssertTrue(sizeof(b) == sizeof(int));
    b.wide  = 0;
    b.value = 7;
    AssertTrue(b.value == 7);
}

[[cccc::test]]
void test_function_specialization(void) {
    AssertTrue(@inst(bool) pick(false, true));
    AssertTrue(@inst(int) pick(3, 9) == 9);
    AssertTrue(pick @(bool)(true, false));
}

[[cccc::test]]
void test_function_specialization_recurses_into_itself(void) {
    AssertTrue(@inst(long) count_down(3) == 100);
    AssertTrue(@inst(int) count_down(3) == 0);
}

[[cccc::test]]
void test_enum_specialization(void) {
    @inst(char) Level lv = Level__char__HIGH;
    AssertTrue(lv == 9);
    AssertTrue(Level__char__LOW == 1);
}

[[cccc::test]]
void test_typedef_specialization(void) {
    @inst(bool) Ptr p = 0;
    @inst(int) Ptr  q = 0;
    AssertTrue(sizeof(p) == sizeof(uint64_t));
    AssertTrue(sizeof(q) == sizeof(int *));
}

[[cccc::test]]
void test_constant_argument_specialization(void) {
    @inst(int, 4) Arr a;
    @inst(int, 5) Arr b;
    AssertTrue(sizeof(a) == 2 * sizeof(int));
    AssertTrue(sizeof(b) == 5 * sizeof(int));
}

[[cccc::test]]
void test_specialization_reached_through_a_default(void) {
    @inst(char) Arr a;
    AssertTrue(sizeof(a) == sizeof(char));
    @inst(int) Arr b;
    AssertTrue(sizeof(b) == 8 * sizeof(int));
}

[[cccc::test]]
void test_specialization_has_its_own_attributes(void) {
    @inst(int) Cell  x = {1, 2, 3}, y = {1, 2, 4};
    @inst(long) Cell z = {1, 1};
    AssertTrue(Cell__int_equals(&x, &x));
    AssertTrue(Cell__long_equals(&z, &z));
}

[[cccc::test]]
void test_nested_template_selects_specialization(void) {
    @inst(bool) Wrap w = {{9, 1}};
    AssertTrue(w.inner.bits == 9);
}

[[cccc::test]]
void test_primary_unaffected_by_other_specialization(void) {
    @inst(int) Plain  a = {1};
    @inst(long) Plain b = {1, 2};
    AssertTrue(sizeof(a) == sizeof(int));
    AssertTrue(sizeof(b) == 2 * sizeof(long));
}

[[cccc::test]]
void test_unused_specialization_costs_nothing(void) {
    AssertTrue(@inst(int) unused(4) == 4);
}
