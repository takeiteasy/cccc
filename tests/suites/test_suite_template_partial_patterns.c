// CCCC_FLAGS: --testing
// Suite: a partial specialization pattern may name a template instance and bind
// an array length.

#include <stddef.h>
#include <string.h>

@template(A, B) struct Pair {
    A a;
    B b;
};
@specialize(char, char) struct Pair {
    char both;
};

@template(T) struct Vec {
    int primary;
    T  *data;
};
@template(T, U) @specialize(Pair @(T, U)) struct Vec {
    int pair;
    T   first;
    U   second;
};
@template(T) @specialize(Pair @(T, T)) struct Vec {
    int same;
    T   both[2];
};

@template(T) struct Ref {
    int primary;
};
@template(T, U) @specialize(const Pair @(T, U) *) struct Ref {
    int to_const_pair;
    T   t;
    U   u;
};

@template(T, int N) struct Fixed {
    T v[N];
};
@template(T) struct Holder {
    int primary;
};
@template(T, int N) @specialize(Fixed @(T, N)) struct Holder {
    int fixed;
    T   items[N];
};

@template(T) struct Box {
    int primary;
    T   v;
};
@template(T, int N) @specialize(T[N]) struct Box {
    int any_len;
    T   items[N];
};
@template(T) @specialize(T[4]) struct Box {
    int four;
    T   items[4];
    int extra;
};

@template(T) struct Mat {
    int primary;
};
@template(T, int N, int M) @specialize(T[N][M]) struct Mat {
    int rows, cols;
    T   cells[N][M];
};

@template(T, int N) struct Arr {
    int primary;
};
@template(T, int N) @specialize(T[N], N) struct Arr {
    int matched;
    T   v[N];
};

@template(int A, int B) struct Grid {
    int primary;
};
@template(int N) @specialize(N, N) struct Grid {
    int  square;
    char cells[N];
};

[[cccc::test]]
void test_instance_pattern_binds_its_arguments(void) {
    Vec @(Pair @(int, float)) v = {.pair = 1, .first = 2, .second = 3.5f};
    AssertEq(v.first, 2);
    AssertTrue(v.second == 3.5f);
}

[[cccc::test]]
void test_repeated_variable_in_an_instance_pattern(void) {
    Vec @(Pair @(int, int)) same;
    Vec @(Pair @(int, long)) mixed;
    AssertEq(sizeof(same.both), 2 * sizeof(int));
    AssertEq(sizeof(mixed.second), sizeof(long));
}

[[cccc::test]]
void test_instance_pattern_matches_through_a_typedef(void) {
    typedef Pair @(long, long) PairLL;
    Vec @(PairLL) v;
    AssertEq(sizeof(v.both), 2 * sizeof(long));
}

[[cccc::test]]
void test_instance_pattern_matches_a_hand_written_specialization(void) {
    Vec @(Pair @(char, char)) v;
    AssertEq(sizeof(v.both), 2);
}

[[cccc::test]]
void test_other_arguments_use_the_primary(void) {
    Vec @(int) v = {.primary = 4};
    AssertEq(v.primary, 4);
}

[[cccc::test]]
void test_instance_pattern_under_a_pointer_and_qualifier(void) {
    Ref @(const Pair @(int, char) *) r = {.to_const_pair = 1, .t = 2, .u = 'c'};
    Ref @(Pair @(int, char) *) plain   = {.primary = 5};
    AssertEq(r.u, 'c');
    AssertEq(plain.primary, 5);
}

[[cccc::test]]
void test_constant_bound_inside_an_instance_pattern(void) {
    Holder @(Fixed @(char, 6)) h;
    Holder @(int) plain = {.primary = 1};
    AssertEq(sizeof(h.items), 6);
    AssertEq(plain.primary, 1);
}

[[cccc::test]]
void test_array_length_variable(void) {
    Box @(int[3]) b;
    Box @(long) plain = {.primary = 1};
    AssertEq(sizeof(b.items), 3 * sizeof(int));
    AssertEq(plain.primary, 1);
}

[[cccc::test]]
void test_exact_length_beats_length_variable(void) {
    Box @(short[4]) b;
    b.extra = 9;
    AssertEq(sizeof(b.items), 4 * sizeof(short));
    AssertEq(b.extra, 9);
}

[[cccc::test]]
void test_two_array_lengths(void) {
    Mat @(int[2][3]) m;
    m.rows = 2;
    m.cols = 3;
    AssertEq(sizeof(m.cells), 6 * sizeof(int));
}

[[cccc::test]]
void test_length_variable_must_equal_a_constant_argument(void) {
    Arr @(int[5], 5) hit;
    Arr @(int[5], 6) miss = {.primary = 1};
    AssertEq(sizeof(hit.v), 5 * sizeof(int));
    AssertEq(miss.primary, 1);
}

[[cccc::test]]
void test_repeated_constant_variable(void) {
    Grid @(3, 3) sq;
    Grid @(3, 4) rect = {.primary = 2};
    AssertEq(sizeof(sq.cells), 3);
    AssertEq(rect.primary, 2);
}
