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
