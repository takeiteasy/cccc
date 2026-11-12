// CCCC_FLAGS: --testing
// Suite: expanding the member pack of a template instance (`t.v...`).

@template(Ts...) struct Tuple {
    Ts... v;
};
@template(Ts...) @specialize(int, Ts...) struct Tuple {
    int first;
};
@template(Ts...) struct Box {
    Ts... v;
    int tag;
};

typedef Tuple @(long, long) Pair;
typedef struct {
    Tuple @(long, long) inner;
} Outer;

static int calls;

static int add0(void) {
    return 5;
}
static int add2(int a, int b) {
    return a * 10 + b;
}
static int add3(int a, int b, int c) {
    return a * 100 + b * 10 + c;
}

static Tuple @(long, long) * count_calls(Tuple @(long, long) * p) {
    calls++;
    return p;
}

@template(T) int in_a_template(void) {
    Tuple @(T, T) t = {4, 2};
    return add2(t.v...);
}

@template(Ts...) int with_a_pack(Ts... xs) {
    Tuple @(Ts...) t = {xs...};
    return (t.v + ... + 0) * 10 + sizeof...(t.v);
}

[[cccc::test]]
void test_call_arguments(void) {
    Tuple @(long, long) a       = {4, 2};
    Tuple @(long, long, long) b = {1, 2, 3};
    AssertEq(add2(a.v...), 42);
    AssertEq(add3(b.v...), 123);
}

[[cccc::test]]
void test_pattern_around_the_member(void) {
    Tuple @(long, long) a = {4, 2};
    AssertEq(add2(a.v * 2 ...), 84);
    AssertEq(add2((a.v + 1)...), 53);
}

[[cccc::test]]
void test_folds(void) {
    Tuple @(long, long, long) b = {1, 2, 3};
    AssertEq((b.v + ...), 6);
    AssertEq((... - b.v), -4);
    AssertEq((10 + ... + b.v), 16);
    AssertEq((b.v * ... * 1), 6);
}

[[cccc::test]]
void test_initializer_list(void) {
    Tuple @(long, long, long) b = {7, 8, 9};
    int a[]                     = {b.v...};
    AssertEq(sizeof(a) / sizeof(a[0]), 3);
    AssertEq(a[2], 9);
}

[[cccc::test]]
void test_length_and_index(void) {
    Tuple @(long, long, long) b = {7, 8, 9};
    AssertEq(sizeof...(b.v), 3);
    AssertEq(b.v...[0], 7);
    AssertEq(b.v...[sizeof...(b.v) - 1], 9);
}

[[cccc::test]]
void test_empty_pack(void) {
    Tuple @() e;
    AssertEq(sizeof...(e.v), 0);
    AssertEq((e.v + ... + 100), 100);
    AssertEq(add0(e.v...), 5);
}

[[cccc::test]]
void test_other_members_of_the_struct(void) {
    Box @(long, long) b = {4, 2, 9};
    AssertEq(add2(b.v...), 42);
    AssertEq(b.tag, 9);
}

[[cccc::test]]
void test_through_a_pointer(void) {
    Tuple @(long, long) a  = {4, 2};
    Tuple @(long, long) *p = &a;
    AssertEq(add2(p->v...), 42);
    AssertEq(add2((*p).v...), 42);
}

[[cccc::test]]
void test_nested_object(void) {
    Outer  o = {{4, 2}};
    Outer *q = &o;
    Pair   p = {1, 2};
    AssertEq(add2(o.inner.v...), 42);
    AssertEq(add2(q->inner.v...), 42);
    AssertEq(add2(p.v...), 12);
}

[[cccc::test]]
void test_object_repeats_per_element(void) {
    Tuple @(long, long) a = {4, 2};
    calls                 = 0;
    AssertEq(add2(count_calls(&a)->v...), 42);
    AssertEq(calls, 2);
}

[[cccc::test]]
void test_two_instances_in_one_pattern(void) {
    Tuple @(long, long) a       = {1, 2};
    Tuple @(long, long) b       = {3, 4};
    Tuple @(long, long, long) c = {5, 6, 7};
    AssertEq(add2(a.v + b.v...), 46);
    AssertEq(add2(a.v...) + add3(c.v...), 12 + 567);
}

[[cccc::test]]
void test_statement_forms(void) {
    Tuple @(long, long) a = {4, 2};
    int r                 = 0;
    if (r == 0)
        r = add2(a.v...);
    AssertEq(r, 42);
    for (int i = 0; i < 2; i++)
        r += (a.v + ...);
    AssertEq(r, 54);
    while (r > 50)
        r -= add2(a.v...) - 40;
    AssertEq(r, 50);
    switch (r) {
        case 50:
            r = add2(a.v...);
            break;
        default:
            r = 0;
    }
    AssertEq(r, 42);
    {
        Tuple @(long) a = {9};
        AssertEq(sizeof...(a.v), 1);
    }
    AssertEq(sizeof...(a.v), 2);
}

[[cccc::test]]
void test_inside_templates(void) {
    AssertEq(in_a_template @(long)(), 42);
    AssertEq(with_a_pack @(long, long, long)(1, 2, 3), 63);
    AssertEq(with_a_pack @()(), 0);
}

[[cccc::test]]
void test_a_partial_can_drop_the_pack(void) {
    Tuple @(int, char) t = {42};
    AssertEq(t.first, 42);
}
