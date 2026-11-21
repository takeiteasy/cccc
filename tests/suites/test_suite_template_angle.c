// CCCC_FLAGS: --testing
// Suite: Name<T> and f<T>(...) instantiate templates like Name@(T).

#include <stddef.h>

@template(T) struct Vec {
    T x, y;
};
@template(T) T max(T a, T b) {
    return a < b ? b : a;
}
@template(T, U) struct Pair {
    T a;
    U b;
};
@template(T, int N) struct Arr {
    T v[N];
};
@template(T) struct Box {
    T v;
    static Box wrap(T v) {
        return (Box){v};
    }
};
@template(T) @specialize(T *) struct Vec {
    char tag[7];
};
@template(T) struct Wrap {
    char tag[1];
};
@template(T) @specialize(Vec<T>) struct Wrap {
    char tag[5];
};
@template(T, U) @specialize(Pair<T, Vec<U>>) struct Wrap {
    char tag[6];
};
@template(T) struct Chain {
    Vec<T>    head;
    Chain<T> *next;
};
@template(T, U) U convert(T x) {
    return (U)x;
}

static Vec<int> make_vec(int a, int b) {
    return (Vec<int>){a, b};
}

[[cccc::test]]
void type_position(void) {
    Vec<int> v = {1, 2};
    AssertEq(v.x + v.y, 3);
}

[[cccc::test]]
void same_instance_as_postfix_spelling(void) {
    Vec<int> a       = {1, 2};
    Vec @(int) b     = a;
    @inst(int) Vec c = b;
    AssertEq(c.y, 2);
    AssertEq(_Generic(a, Vec @(int): 1, default: 0), 1);
}

[[cccc::test]]
void nested_close_splits(void) {
    Vec<Vec<int>>      vv  = {{1, 2}, {3, 4}};
    Vec<Vec<Vec<int>>> vvv = {vv, vv};
    AssertEq(vv.y.x, 3);
    AssertEq(vvv.x.y.y, 4);
}

[[cccc::test]]
void two_arguments(void) {
    Pair<int, Vec<int>> p = {7, {8, 9}};
    AssertEq(p.a + p.b.y, 16);
}

[[cccc::test]]
void constant_argument(void) {
    Arr<int, 3>               a = {{1, 2, 3}};
    Arr<int, (2 > 1 ? 4 : 5)> b = {{1, 2, 3, 4}};
    AssertEq((int)sizeof(a.v) / (int)sizeof(int), 3);
    AssertEq((int)sizeof(b.v) / (int)sizeof(int), 4);
}

[[cccc::test]]
void function_template_call(void) {
    AssertEq(max<int>(1, 2), 2);
    AssertEq(max<long>(5, 3), 5);
}

@template(T) T fact(T n) {
    return n <= 1 ? 1 : n * fact<T>(n - 1);
}

[[cccc::test]]
void function_template_calls_itself(void) {
    AssertEq(fact<int>(5), 120);
    AssertEq(fact<long>(6), 720L);
}

[[cccc::test]]
void explicit_prefix_then_deduction(void) {
    AssertEq((convert<int, long>(7)), 7L);
    AssertEq((convert<double, int>(2.5)), 2);
}

[[cccc::test]]
void cast_and_sizeof(void) {
    Vec<int> v = {1, 2};
    void    *p = &v;
    AssertEq(((Vec<int> *)p)->y, 2);
    AssertEq((int)sizeof(Vec<int>), (int)sizeof(int) * 2);
}

[[cccc::test]]
void static_method_through_instance(void) {
    AssertEq(Box<int>.wrap(5).v, 5);
}

[[cccc::test]]
void partial_specialization_pattern(void) {
    Vec<int *>                 p;
    Wrap<int>                  a;
    Wrap<Vec<int>>             b;
    Wrap<Pair<int, Vec<long>>> c;
    AssertEq((int)sizeof(p.tag), 7);
    AssertEq((int)sizeof(a.tag), 1);
    AssertEq((int)sizeof(b.tag), 5);
    AssertEq((int)sizeof(c.tag), 6);
}

[[cccc::test]]
void self_reference_in_template(void) {
    Chain<int> c = {{1, 2}, NULL};
    AssertEq(c.head.y, 2);
}

[[cccc::test]]
void function_returning_instance(void) {
    AssertEq(make_vec(3, 4).y, 4);
}

[[cccc::test]]
void local_shadow_stays_less_than(void) {
    int max = 3, Vec = 1;
    AssertEq(Vec < max, 1);
    AssertEq(max > Vec, 1);
}

[[cccc::test]]
void other_less_than_untouched(void) {
    int a = 1, b = 2;
    AssertEq(a < b, 1);
    AssertEq(a < b, 1);
}
