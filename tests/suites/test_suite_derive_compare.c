// CCCC_FLAGS: --testing
// Suite: @compare, run on the VM and under -c=native.

#include <cccc/derive.h>
#include <string.h>

typedef enum { RED, GREEN, BLUE } Color;

@equals @compare struct Inner {
    int  a;
    char tag[4];
};

@equals @compare struct Everything {
    char           c;
    unsigned short us;
    long           l;
    _Bool          b;
    Color          color;
    double         d;
    unsigned       lo : 3;
    int            hi : 5;
    char           name[8];
    int            arr[3];
    struct Inner   inner;
    struct {
        int x;
        int y;
    };
    int cache @equals_skip;
};

@compare("app") struct Pair {
    int a;
    int b;
};

@compare typedef struct {
    unsigned char u;
    float         f;
} Mixed;

static void fill(struct Everything *e) {
    memset(e, 0, sizeof *e);
    e->c     = 'x';
    e->us    = 7;
    e->l     = -9;
    e->b     = 1;
    e->color = GREEN;
    e->d     = 1.5;
    e->lo    = 5;
    e->hi    = -3;
    strcpy(e->name, "ann");
    e->arr[0]  = 1;
    e->arr[1]  = 2;
    e->arr[2]  = 3;
    e->inner.a = 4;
    strcpy(e->inner.tag, "ab");
    e->x = 10;
    e->y = 11;
}

@compare struct Ranked {
    int a;
    int b @compare_order(2);
    int c @compare_order(1);
    int d @compare_order(1);
    int e @compare_desc;
    int f @compare_order(-5) @compare_desc;
};

@compare struct Layers {
    struct Inner in @compare_desc;
    struct {
        int low;
        int high @compare_order(0);
    };
    char name[4] @compare_desc;
    int  arr[2] @compare_order(3) @compare_desc;
};

[[cccc::test]] void test_equal_values_compare_zero(void) {
    struct Everything a, b;
    fill(&a);
    fill(&b);
    a.cache = 1;
    b.cache = 2;
    AssertEq(Everything_compare(&a, &b), 0);
    AssertEq(!!Everything_equals(&a, &b), 1);
}

[[cccc::test]] void test_each_member_orders(void) {
    struct Everything base, v;
    fill(&base);

#define CHECK(stmt, sign)                                                      \
    do {                                                                       \
        fill(&v);                                                              \
        stmt;                                                                  \
        int r = Everything_compare(&base, &v);                                 \
        int q = Everything_compare(&v, &base);                                 \
        AssertEq(r < 0, (sign) < 0);                                           \
        AssertEq(r > 0, (sign) > 0);                                           \
        AssertEq(q < 0, (sign) > 0);                                           \
        AssertEq(q > 0, (sign) < 0);                                           \
    } while (0)
    CHECK(v.c = 'y', -1);
    CHECK(v.c = 'a', 1);
    CHECK(v.us = 8, -1);
    CHECK(v.l = 0, -1);
    CHECK(v.l = -10, 1);
    CHECK(v.b = 0, 1);
    CHECK(v.color = BLUE, -1);
    CHECK(v.d = 2.0, -1);
    CHECK(v.lo = 6, -1);
    CHECK(v.hi = -4, 1);
    CHECK(v.name[0] = 'b', -1);
    CHECK(v.arr[2] = 9, -1);
    CHECK(v.inner.a = 3, 1);
    CHECK(v.inner.tag[1] = 'c', -1);
    CHECK(v.x = 9, 1);
    CHECK(v.y = 12, -1);
#undef CHECK
}

[[cccc::test]] void test_declaration_order_decides(void) {
    struct Pair a = {1, 9};
    struct Pair b = {2, 0};
    AssertEq(app_Pair_compare(&a, &b) < 0, 1);
    AssertEq(app_Pair_compare(&b, &a) > 0, 1);
    b.a = 1;
    AssertEq(app_Pair_compare(&a, &b) > 0, 1);
}

[[cccc::test]] void test_strings_are_unsigned_and_stop_at_nul(void) {
    struct Inner a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    strcpy(a.tag, "ab");
    strcpy(b.tag, "ab");
    a.tag[3] = 'x';
    b.tag[3] = 'y';
    AssertEq(Inner_compare(&a, &b), 0);
    a.tag[0] = (char)0xff;
    b.tag[0] = 'a';
    AssertEq(Inner_compare(&a, &b) > 0, 1);
}

[[cccc::test]] void test_typedef_and_float(void) {
    Mixed a = {1, 1.5f};
    Mixed b = {1, 2.5f};
    AssertEq(Mixed_compare(&a, &b) < 0, 1);
    b.f = 1.5f;
    AssertEq(Mixed_compare(&a, &b), 0);
}

[[cccc::test]] void test_order_keys_come_first_ascending(void) {
    struct Ranked x = {0, 0, 0, 0, 0, 0};
    struct Ranked y = x;

    y.f             = 1;
    AssertTrue(Ranked_compare(&x, &y) > 0);
    y   = x;
    y.f = 1;
    y.c = -9;
    y.d = -9;
    y.b = -9;
    y.a = -9;
    AssertTrue(Ranked_compare(&x, &y) > 0);

    y   = x;
    y.c = 1;
    y.d = -1;
    y.b = -1;
    AssertTrue(Ranked_compare(&x, &y) < 0);

    y   = x;
    y.d = 1;
    y.b = -1;
    AssertTrue(Ranked_compare(&x, &y) < 0);

    y   = x;
    y.b = 1;
    y.a = -1;
    AssertTrue(Ranked_compare(&x, &y) < 0);

    y   = x;
    y.a = 1;
    y.e = 1;
    AssertTrue(Ranked_compare(&x, &y) < 0);
}

[[cccc::test]] void test_ties_keep_declaration_order(void) {
    struct Ranked x = {0, 0, 0, 0, 0, 0};
    struct Ranked y = x;
    y.c             = 1;
    y.d             = -1;
    AssertTrue(Ranked_compare(&x, &y) < 0);
    y.c = 0;
    y.d = 1;
    AssertTrue(Ranked_compare(&x, &y) < 0);
    y.d = 0;
    AssertEq(Ranked_compare(&x, &y), 0);
}

[[cccc::test]] void test_desc_reverses_direction(void) {
    struct Ranked x = {0, 0, 0, 0, 0, 0};
    struct Ranked y = x;
    y.e             = 5;
    AssertTrue(Ranked_compare(&x, &y) > 0);
    AssertTrue(Ranked_compare(&y, &x) < 0);
    y.e = -5;
    AssertTrue(Ranked_compare(&x, &y) < 0);
    y.e = 0;
    y.f = -2;
    AssertTrue(Ranked_compare(&x, &y) < 0);
}

[[cccc::test]] void test_order_in_nested_anonymous_and_array(void) {
    struct Layers x;
    memset(&x, 0, sizeof x);
    struct Layers y = x;

    y.high          = 1;
    y.in.a          = 1;
    AssertTrue(Layers_compare(&x, &y) < 0);

    y      = x;
    y.low  = 1;
    y.in.a = 1;
    AssertTrue(Layers_compare(&x, &y) > 0);

    y     = x;
    y.low = 1;
    AssertTrue(Layers_compare(&x, &y) < 0);

    y         = x;
    y.name[0] = 'a';
    AssertTrue(Layers_compare(&x, &y) > 0);

    y        = x;
    y.arr[1] = 1;
    AssertTrue(Layers_compare(&x, &y) > 0);
}
