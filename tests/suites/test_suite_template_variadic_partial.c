// CCCC_FLAGS: --testing
// Suite: a partial specialization of a template with a parameter pack.

#include <stddef.h>

// A pattern constrains the leading arguments and a pack variable takes the
// rest.
@template(Ts...) struct Tuple {
    Ts... v;
    int kind;
};
@template(T, Ts...) @specialize(T *, Ts...) struct Tuple {
    T *head;
    Ts... tail;
    int count[sizeof...(Ts) + 1];
};
@template(Ts...) @specialize(int, Ts...) struct Tuple {
    int  first;
    char count[sizeof...(Ts) + 1];
};
@template(T) @specialize(T *) struct Tuple {
    T   *only;
    char extra[3];
};

// The most specialised partial wins; fewer pack arguments are more specific.
@template(Ts...) struct Rank {
    char v[1];
};
@template(T, Ts...) @specialize(T, Ts...) struct Rank {
    char v[2];
};
@template(T, U, Ts...) @specialize(T, U, Ts...) struct Rank {
    char v[3];
};
@template(T, U) @specialize(T, U) struct Rank {
    char v[4];
};
@template(T) @specialize(T *) struct Rank {
    char v[5];
};

@template(T, Ts...) struct Lead {
    T a;
};
@template(T, Ts...) @specialize(T, T, Ts...) struct Lead {
    T a, b;
};
@template(T, Ts...) @specialize(T, int, Ts...) struct Lead {
    T   a;
    int b;
};
@template(Ts...) @specialize(int, int, Ts...) struct Lead {
    int a, b, c;
};

// The pack of the partial is its own: folds, indexes and declarations.
@template(Ts...) int total(Ts... xs) {
    return (xs + ... + 0);
}
@template(T, Ts...) @specialize(T *, Ts...) int total(T *p, Ts... xs) {
    return *p * 100 + (xs + ... + 0);
}
@template(Ts...) @specialize(char, Ts...) int total(char c, Ts... xs) {
    return c + sizeof...(Ts) * 1000 + (xs + ... + 0);
}

@template(Ts...) struct Sizes {
    int n;
};
@template(T, Ts...) @specialize(T *, Ts...) struct Sizes {
    int  n;
    char first[sizeof(T)];
    char rest[(sizeof(Ts) + ... + 0) + 1];
    Ts... items;
};

// A constant pack is matched the same way.
@template(T, int... Ns) struct Nd {
    int n[sizeof...(Ns) + 1];
};
@template(T, int... Ms) @specialize(T, 0, Ms...) struct Nd {
    int zero;
    int rest[(Ms + ... + 0) + 1];
};
@template(T, int N, int... Ms) @specialize(T, N, N, Ms...) struct Nd {
    char twin[N];
    char more[sizeof...(Ms) + 1];
};

@template(int... Ns) struct Dim {
    char v[1];
};
@template(int A, int... Ns) @specialize(A, Ns...) struct Dim {
    char v[A + (Ns + ... + 0)];
};

// A pattern names an instance of a variadic template.
@template(Ts...) struct Pack {
    Ts... m;
};
@template(T) struct Wrap {
    T x;
};
@template(Ts...) @specialize(Pack @(Ts...)) struct Wrap {
    char v[2];
};
@template(T, Ts...) @specialize(Pack @(T, Ts...)) struct Wrap {
    char v[3 + sizeof...(Ts)];
};
@template(Ts...) @specialize(Pack @(int, Ts...)) struct Wrap {
    char v[10 + sizeof...(Ts)];
};

@template(T, U) struct Pair {
    T t;
    U u;
};
@template(T, U, Ts...) @specialize(Pair @(Pack @(T, Ts...), U)) struct Wrap {
    char v[20 + sizeof...(Ts)];
};

@template(T) struct Box {
    T x;
};
@template(T, Ts...) @specialize(Box @(Pack @(T, Ts...))) struct Wrap {
    char v[30];
};

// A variable may repeat before the tail.
@template(T) struct Seq {
    T v;
};
@template(T, Ts...) @specialize(Pack @(T, T, Ts...)) struct Seq {
    T    a, b;
    char rest[sizeof...(Ts) + 1];
};

// An element pattern: every argument after the leading ones must match it, and
// the pack variable takes what each one matched.
@template(Ts...) struct Ptrs {
    char kind[1];
};
@template(Ts...) @specialize(Ts *...) struct Ptrs {
    char kind[2];
    Ts... pointee;
};
@template(Ts...) @specialize(Ts **...) struct Ptrs {
    char kind[3];
    Ts... pointee;
};
@template(Ts...) struct CPtrs {
    char kind[1];
};
@template(Ts...) @specialize(const Ts *...) struct CPtrs {
    char kind[4];
    Ts... pointee;
};

@template(T) struct Cell {
    T x;
};
@template(Ts...) struct Cells {
    char kind[1];
};
@template(Ts...) @specialize(Cell @(Ts)...) struct Cells {
    char kind[2];
    Ts... inner;
};

@template(T, U) struct Link {
    T a;
    U b;
};
@template(Ts...) struct Links {
    char kind[1];
};
@template(T, Ts...) @specialize(T, Link @(T, Ts)...) struct Links {
    char kind[2];
    Ts... rest;
};

@template(Ts...) struct Outer {
    char kind[1];
};
@template(Ts...) @specialize(Ptrs @(Ts *...)) struct Outer {
    char kind[2];
};

@template(Ts...) struct PtrLead {
    char kind[1];
};
@template(Ts...) @specialize(int *, Ts...) struct PtrLead {
    char kind[2];
};
@template(Ts...) @specialize(Ts *...) struct PtrLead {
    char kind[3];
};
@template(Ts...) @specialize(int *, Ts *...) struct PtrLead {
    char kind[4];
};

[[cccc::test]]
void test_leading_pointer_and_pack_tail(void) {
    int                            x = 7;
    @inst(char *, int, long) Tuple t;
    t.head  = (char *)&x;
    t.tail0 = 1;
    t.tail1 = 2;
    AssertEq(t.tail0 + t.tail1, 3);
    AssertEq(sizeof(t.count), 3 * sizeof(int));
}

[[cccc::test]]
void test_empty_tail(void) {
    @inst(short *) Tuple one;
    @inst(int) Tuple     i;
    AssertEq(sizeof(one.extra), 3);
    AssertEq(sizeof(i.count), 1);
}

[[cccc::test]]
void test_literal_leading_argument(void) {
    @inst(int, char, char, char) Tuple t;
    t.first = 1;
    AssertEq(sizeof(t.count), 4);
}

[[cccc::test]]
void test_other_arguments_use_the_primary(void) {
    @inst(char, short) Tuple p;
    p.v0   = 1;
    p.v1   = 2;
    p.kind = 3;
    AssertEq(p.v0 + p.v1 + p.kind, 6);
    @inst Tuple e;
    e.kind = 4;
    AssertEq(e.kind, 4);
}

[[cccc::test]]
void test_exact_length_beats_open_ended(void) {
    @inst(int *) Tuple      one;
    @inst(int *, int) Tuple two;
    AssertEq(sizeof(one.extra), 3);
    AssertEq(sizeof(two.count), 2 * sizeof(int));
}

[[cccc::test]]
void test_rank_by_length(void) {
    @inst Rank                         e;
    @inst(char) Rank                   a;
    @inst(char, char) Rank             b;
    @inst(char, char, char) Rank       c;
    @inst(char, char, char, char) Rank d;
    @inst(char *) Rank                 p;
    AssertEq(sizeof(e.v), 1);
    AssertEq(sizeof(a.v), 2);
    AssertEq(sizeof(b.v), 4);
    AssertEq(sizeof(c.v), 3);
    AssertEq(sizeof(d.v), 3);
    AssertEq(sizeof(p.v), 5);
}

[[cccc::test]]
void test_literal_beats_variable(void) {
    @inst(char, char) Lead     a;
    @inst(char, int) Lead      b;
    @inst(int, int, long) Lead c;
    @inst(int, long) Lead      d;
    AssertEq(sizeof(a.b), 1);
    AssertEq(sizeof(b.b), sizeof(int));
    AssertEq(sizeof(c), 3 * sizeof(int));
    AssertEq(sizeof(d.a), sizeof(int));
}

[[cccc::test]]
void test_function_partials(void) {
    int v = 3;
    AssertEq(total @(int, int)(1, 2), 3);
    AssertEq(total @(int *, int, int)(&v, 4, 5), 309);
    AssertEq(total @(char, int, long)('a', 1, 2), 97 + 2000 + 3);
    AssertEq(total @(char)('b'), 98);
}

[[cccc::test]]
void test_pack_variable_in_the_body(void) {
    @inst(long *, int, char) Sizes s;
    AssertEq(sizeof(s.first), sizeof(long));
    AssertEq(sizeof(s.rest), sizeof(int) + sizeof(char) + 1);
    s.items0 = 5;
    s.items1 = 6;
    AssertEq(s.items0 + s.items1, 11);
    @inst(int *) Sizes z;
    AssertEq(sizeof(z.rest), 1);
}

[[cccc::test]]
void test_constant_pack_tail(void) {
    @inst(char, 0, 2, 3) Nd a;
    @inst(char, 1, 2) Nd    b;
    AssertEq(sizeof(a.rest), 6 * sizeof(int));
    AssertEq(sizeof(b.n), 3 * sizeof(int));
    a.zero = 1;
    AssertEq(a.zero, 1);
}

[[cccc::test]]
void test_constant_variable_repeated_with_a_pack(void) {
    @inst(char, 4, 4, 9, 9) Nd t;
    AssertEq(sizeof(t.twin), 4);
    AssertEq(sizeof(t.more), 3);
    @inst(char, 4, 5, 9, 9) Nd u;
    AssertEq(sizeof(u.n), 5 * sizeof(int));
}

[[cccc::test]]
void test_constant_pack_variable_alone(void) {
    @inst Dim          e;
    @inst(5) Dim       a;
    @inst(2, 3, 4) Dim b;
    AssertEq(sizeof(e.v), 1);
    AssertEq(sizeof(a.v), 5);
    AssertEq(sizeof(b.v), 9);
}

[[cccc::test]]
void test_pattern_over_a_variadic_instance(void) {
    @inst(Pack @()) Wrap                e;
    @inst(Pack @(char)) Wrap            one;
    @inst(Pack @(char, long)) Wrap      two;
    @inst(Pack @(int)) Wrap             lit;
    @inst(Pack @(int, char, char)) Wrap lit3;
    AssertEq(sizeof(e.v), 2);
    AssertEq(sizeof(one.v), 3);
    AssertEq(sizeof(two.v), 4);
    AssertEq(sizeof(lit.v), 10);
    AssertEq(sizeof(lit3.v), 12);
}

[[cccc::test]]
void test_instance_pattern_nested(void) {
    @inst(Pair @(Pack @(char, int, int), long)) Wrap p;
    AssertEq(sizeof(p.v), 22);
    @inst(Box @(Pack @(char, int))) Wrap b;
    AssertEq(sizeof(b.v), 30);
}

[[cccc::test]]
void test_instance_names_ignore_the_partial(void) {
    @inst(char *, int, long) Tuple     a;
    struct Tuple__char_ptr__int__long *b = &a;
    AssertTrue(b == &a);
    @inst(int, char) Tuple   c;
    struct Tuple__int__char *d = &c;
    AssertTrue(d == &c);
}

[[cccc::test]]
void test_repeated_variable_before_a_pack_tail(void) {
    @inst(Pack @(short, short, char)) Seq s;
    AssertEq(sizeof(s.rest), 2);
}

[[cccc::test]]
void test_element_pattern_selects_by_every_argument(void) {
    @inst(int *, char *) Ptrs   a;
    @inst(int *, char) Ptrs     b;
    @inst(int, char *) Ptrs     c;
    @inst() Ptrs                d;
    @inst(int **, char **) Ptrs e;
    @inst(int **, char *) Ptrs  f;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(b.kind), 1);
    AssertEq(sizeof(c.kind), 1);
    AssertEq(sizeof(d.kind),
             3); // the empty pack matches both; `Ts **...` is narrower
    AssertEq(sizeof(e.kind), 3);
    AssertEq(sizeof(f.kind), 2);
}

[[cccc::test]]
void test_element_pattern_binds_what_each_argument_matched(void) {
    @inst(int *, char *, long *) Ptrs a;
    AssertEq(sizeof(a.pointee0), sizeof(int));
    AssertEq(sizeof(a.pointee1), sizeof(char));
    AssertEq(sizeof(a.pointee2), sizeof(long));
    @inst(int **, long **) Ptrs b;
    AssertEq(sizeof(b.pointee0), sizeof(int));
    AssertEq(sizeof(b.pointee1), sizeof(long));
}

[[cccc::test]]
void test_element_pattern_may_carry_qualifiers(void) {
    @inst(const int *, const char *) CPtrs a;
    AssertEq(sizeof(a.kind), 4);
    AssertEq(sizeof(a.pointee0), sizeof(int));
    @inst(const int *, char *) CPtrs b;
    AssertEq(sizeof(b.kind), 1);
}

[[cccc::test]]
void test_element_pattern_names_an_instance(void) {
    @inst(Cell @(int), Cell @(char)) Cells a;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(a.inner0), sizeof(int));
    AssertEq(sizeof(a.inner1), sizeof(char));
    @inst(Cell @(int), char) Cells b;
    AssertEq(sizeof(b.kind), 1);
}

[[cccc::test]]
void test_element_pattern_shares_a_leading_variable(void) {
    @inst(int, Link @(int, char), Link @(int, long)) Links a;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(a.rest0), sizeof(char));
    AssertEq(sizeof(a.rest1), sizeof(long));
    @inst(int, Link @(int, char), Link @(short, long)) Links b;
    AssertEq(sizeof(b.kind), 1);
    @inst(int) Links c;
    AssertEq(sizeof(c.kind), 2);
}

[[cccc::test]]
void test_element_pattern_in_a_pattern_instance(void) {
    @inst(Ptrs @(int *, char *)) Outer a;
    AssertEq(sizeof(a.kind), 2);
    @inst(Ptrs @(int *, char)) Outer b;
    AssertEq(sizeof(b.kind), 1);
}

[[cccc::test]]
void test_element_pattern_ranks_with_leading_items(void) {
    @inst(int *, char *) PtrLead  a;
    @inst(int *, char) PtrLead    b;
    @inst(char *, long *) PtrLead c;
    @inst(char, long) PtrLead     d;
    AssertEq(sizeof(a.kind), 4);
    AssertEq(sizeof(b.kind), 2);
    AssertEq(sizeof(c.kind), 3);
    AssertEq(sizeof(d.kind), 1);
}

// Float and pointer constant packs.
@template(double... Ds) double fold(void) {
    return 0;
}
@template(double... Ds) @specialize(1.0, Ds...) double fold(void) {
    return 100 + sizeof...(Ds) + (Ds + ... + 0.0);
}

static int vp_a = 3, vp_b = 4;

@template(int *... Ps) int deref(void) {
    return 0;
}
@template(int *... Ps) @specialize(&vp_a, Ps...) int deref(void) {
    return 10 * sizeof...(Ps) + (*Ps + ... + 0);
}

[[cccc::test]]
void test_float_pack_variable(void) {
    AssertEq(fold @(1.0, 2.0, 4.0)(), 108.0);
    AssertEq(fold @(1.0)(), 100.0);
    AssertEq(fold @(2.0, 4.0)(), 0.0);
    AssertEq(fold @()(), 0.0);
}

[[cccc::test]]
void test_pointer_pack_variable(void) {
    AssertEq(deref @(&vp_a, &vp_b, &vp_a)(), 27);
    AssertEq(deref @(&vp_a)(), 0);
    AssertEq(deref @(&vp_b, &vp_a)(), 0);
}

// The pack of a dependent primary takes whatever its first argument is, so a
// partial over integers does not match a float instance.
@template(T, T... Vs) int dep(void) {
    return 0;
}
@template(int... Ns) @specialize(int, Ns...) int dep(void) {
    return 1 + sizeof...(Ns);
}
@template(int... Ns) @specialize(double, Ns...) int dep(void) {
    return 99;
}

[[cccc::test]]
void test_dependent_pack_skips_a_partial_of_the_wrong_kind(void) {
    AssertEq(dep @(int, 1, 2)(), 3);
    AssertEq(dep @(int)(), 1);
    AssertEq(dep @(double, 1.0)(), 0);
}

// A pack variable used more than once stands for one argument list.
@template(Ts...) struct Bag {
    char k;
};
@template(Ts...) struct Bag2 {
    char k;
};
@template(Ts...) struct Twin {
    char kind;
};
@template(Ts...) @specialize(Bag @(Ts...), Ts...) struct Twin {
    char kind[2];
};
@template(T, Ts...) @specialize(T, Ts...) struct Twin {
    char kind[3];
};

[[cccc::test]]
void test_repeated_pack_variable_needs_equal_lists(void) {
    @inst(Bag @(int, char), int, char) Twin       a;
    @inst(Bag @(int, char), int, long) Twin       b;
    @inst(Bag @(int, char), int) Twin             c;
    @inst(Bag @(int, char), int, char, char) Twin d;
    @inst(Bag @(int, char), char, int) Twin       e;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(b.kind), 3);
    AssertEq(sizeof(c.kind), 3);
    AssertEq(sizeof(d.kind), 3);
    AssertEq(sizeof(e.kind), 3);
}

[[cccc::test]]
void test_repeated_pack_variable_matches_empty_lists(void) {
    @inst(Bag @()) Twin a;
    @inst(int) Twin     b;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(b.kind), 3);
}

[[cccc::test]]
void test_repeated_pack_variable_compares_qualified_types(void) {
    @inst(Bag @(int *, const char), int *, const char) Twin a;
    @inst(Bag @(int *, const char), int *, char) Twin       b;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(b.kind), 3);
}

@template(Ts...) struct PtrTwin {
    char kind;
};
@template(Ts...) @specialize(Bag @(Ts *...), Ts...) struct PtrTwin {
    char kind[2];
};
@template(Ts...) @specialize(Bag @(Ts...), Ts...) struct PtrTwin {
    char kind[3];
};

[[cccc::test]]
void test_repeated_pack_variable_with_an_element_pattern(void) {
    @inst(Bag @(int *, char *), int, char) PtrTwin     a;
    @inst(Bag @(int *, char *), int, long) PtrTwin     b;
    @inst(Bag @(int, char), int, char) PtrTwin         c;
    @inst(Bag @(int *, char *), int *, char *) PtrTwin d;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(b.kind), 1);
    AssertEq(sizeof(c.kind), 3);
    AssertEq(sizeof(d.kind), 3);
}

@template(int... Ns) struct Row {
    char k;
};
@template(T, int... Ns) struct Grid {
    char kind;
};
@template(int... Ns) @specialize(Row @(Ns...), Ns...) struct Grid {
    char kind[2];
};

[[cccc::test]]
void test_repeated_constant_pack_variable(void) {
    @inst(Row @(1, 2), 1, 2) Grid a;
    @inst(Row @(1, 2), 1, 3) Grid b;
    @inst(Row @(1, 2), 1) Grid    c;
    @inst(Row @()) Grid           d;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(b.kind), 1);
    AssertEq(sizeof(c.kind), 1);
    AssertEq(sizeof(d.kind), 2);
}

@template(double... Ds) struct FRow {
    char k;
};
@template(T, double... Ds) struct FGrid {
    char kind;
};
@template(double... Ds) @specialize(FRow @(Ds...), Ds...) struct FGrid {
    char kind[2];
};

[[cccc::test]]
void test_repeated_float_pack_variable(void) {
    @inst(FRow @(1.5, 2.5), 1.5, 2.5) FGrid a;
    @inst(FRow @(1.5, 2.5), 1.5, 2.0) FGrid b;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(b.kind), 1);
}

@template(A, B) struct Join {
    char kind;
};
@template(Ts...) @specialize(Bag @(Ts...), Bag2 @(Ts...)) struct Join {
    char kind[2];
};

[[cccc::test]]
void test_pack_variable_shared_by_two_instance_patterns(void) {
    @inst(Bag @(int, char), Bag2 @(int, char)) Join a;
    @inst(Bag @(int, char), Bag2 @(int, long)) Join b;
    @inst(Bag @(int, char), Bag2 @(int)) Join       c;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(b.kind), 1);
    AssertEq(sizeof(c.kind), 1);
}

@template(T, U = T, Ts...) struct Dp {
    char kind[1];
};
@template(Ts...) @specialize(int, Ts...) struct Dp {
    char kind[2];
};
@template(Ts...) @specialize(int, char, Ts...) struct Dp {
    char kind[3];
};
@template(T, U = T, Ts...) struct Dq {
    char kind[1];
};
@template(T) @specialize(T, T) struct Dq {
    char kind[2];
};
@template(T, Ts...) @specialize(T, Ts *...) struct Dq {
    char kind[3];
};

[[cccc::test]]
void test_pack_variable_spans_a_defaulted_parameter(void) {
    @inst(int) Dp a;
    @inst(int, long) Dp b;
    @inst(long) Dp c;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(b.kind), 2);
    AssertEq(sizeof(c.kind), 1);
}

[[cccc::test]]
void test_narrower_partial_wins_across_the_default(void) {
    @inst(int, char) Dp a;
    @inst(int, char, long) Dp b;
    AssertEq(sizeof(a.kind), 3);
    AssertEq(sizeof(b.kind), 3);
}

[[cccc::test]]
void test_pattern_without_pack_variable_stops_early(void) {
    @inst(int, int) Dq  a;
    @inst(int, int, long) Dq b;
    AssertEq(sizeof(a.kind), 2);
    AssertEq(sizeof(b.kind), 1);
}
