// CCCC_FLAGS: --testing
// Suite: @equals / @hash, run on the VM and under -c=native.

#include <cccc/derive.h>
#include <string.h>

typedef enum { RED, GREEN } Color;

@equals @hash struct Inner {
    int  a;
    char tag[4];
};

@equals @hash struct Everything {
    char           c;
    unsigned short us;
    long           l;
    _Bool          b;
    Color          color;
    double         d;
    float          f;
    long double    ld;
    int           *ptr;
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

@equals("app") @hash("app") struct Pair {
    int a;
    int b;
};

static int shared;

// Two structs with different padding/garbage, same member values.
static void fill(struct Everything *e, int fill_byte) {
    memset(e, fill_byte, sizeof *e);
    e->c     = 'x';
    e->us    = 7;
    e->l     = -9;
    e->b     = 1;
    e->color = GREEN;
    e->d     = 0.0;
    e->f     = 1.5f;
    e->ld    = 2.25L;
    e->ptr   = &shared;
    e->lo    = 5;
    e->hi    = -3;
    memset(e->name, fill_byte, sizeof e->name);
    strcpy(e->name, "ann");
    e->arr[0]  = 1;
    e->arr[1]  = 2;
    e->arr[2]  = 3;
    e->inner.a = 4;
    memset(e->inner.tag, fill_byte, sizeof e->inner.tag);
    strcpy(e->inner.tag, "ab");
    e->x = 10;
    e->y = 11;
}

[[cccc::test]] void test_equal_and_hash_ignore_garbage(void) {
    struct Everything a, b;
    fill(&a, 0x00);
    fill(&b, 0x5a);
    a.d     = -0.0;
    b.d     = 0.0;
    a.cache = 1;
    b.cache = 2;
    AssertEq(!!(Everything_equals(&a, &b)), 1);
    AssertEq(!!(Everything_hash(&a) == Everything_hash(&b)), 1);
}

[[cccc::test]] void test_each_member_matters(void) {
    struct Everything base, v;
    fill(&base, 1);

#define CHECK(stmt)                                                            \
    do {                                                                       \
        fill(&v, 2);                                                           \
        stmt;                                                                  \
        AssertEq(!!(Everything_equals(&base, &v)), 0);                         \
    } while (0)
    CHECK(v.c = 'y');
    CHECK(v.us = 8);
    CHECK(v.l = 0);
    CHECK(v.b = 0);
    CHECK(v.color = RED);
    CHECK(v.d = 1.0);
    CHECK(v.f = 2.0f);
    CHECK(v.ld = 3.0L);
    CHECK(v.ptr = NULL);
    CHECK(v.lo = 6);
    CHECK(v.hi = 3);
    CHECK(v.name[1] = 'X');
    CHECK(v.arr[2] = 9);
    CHECK(v.inner.a = 5);
    CHECK(v.inner.tag[1] = 'c');
    CHECK(v.x = 0);
    CHECK(v.y = 0);
#undef CHECK
}

[[cccc::test]] void test_string_member_stops_at_nul(void) {
    struct Inner a = {1, {'a', 0, 'x', 'y'}};
    struct Inner b = {1, {'a', 0, 'p', 'q'}};
    AssertEq(!!(Inner_equals(&a, &b)), 1);
    AssertEq(!!(Inner_hash(&a) == Inner_hash(&b)), 1);

    struct Inner full1 = {1, {'a', 'b', 'c', 'd'}};
    struct Inner full2 = {1, {'a', 'b', 'c', 'd'}};
    AssertEq(!!(Inner_equals(&full1, &full2)), 1);
    full2.tag[3] = 'e';
    AssertEq(!!(Inner_equals(&full1, &full2)), 0);
}

[[cccc::test]] void test_skipped_member_changes_nothing(void) {
    struct Everything a, b;
    fill(&a, 3);
    fill(&b, 3);
    b.cache = 99;
    AssertEq(!!(Everything_equals(&a, &b)), 1);
    AssertEq(!!(Everything_hash(&a) == Everything_hash(&b)), 1);
}

[[cccc::test]] void test_hash_distinguishes_values(void) {
    struct Everything a, b;
    fill(&a, 0);
    fill(&b, 0);
    b.inner.a = 5;
    AssertEq(!!(Everything_hash(&a) != Everything_hash(&b)), 1);
}

[[cccc::test]] void test_prefix_and_const_pointer(void) {
    const struct Pair a = {1, 2};
    const struct Pair b = {1, 2};
    const struct Pair c = {1, 3};
    AssertEq(!!(app_Pair_equals(&a, &b)), 1);
    AssertEq(!!(app_Pair_equals(&a, &c)), 0);
    AssertEq(!!(app_Pair_hash(&a) == app_Pair_hash(&b)), 1);
    AssertEq(!!(app_Pair_hash(&a) != app_Pair_hash(&c)), 1);
}

@equals @hash struct Grid {
    int grid[2][2];
};

[[cccc::test]] void test_equals_hash_array_of_arrays(void) {
    struct Grid a = {{{1, 2}, {3, 4}}};
    struct Grid b = a;
    struct Grid c = a;
    c.grid[1][0]  = 9;
    AssertEq(!!(Grid_equals(&a, &b)), 1);
    AssertEq(!!(Grid_equals(&a, &c)), 0);
    AssertEq(!!(Grid_hash(&a) == Grid_hash(&b)), 1);
    AssertEq(!!(Grid_hash(&a) != Grid_hash(&c)), 1);
}

[[cccc::test]] void test_equals_binds_operators(void) {
    struct Inner       a = {1, "ab"};
    struct Inner       b = {1, "ab"};
    struct Inner       c = {2, "ab"};
    const struct Inner k = a;
    AssertEq(!!(a == b), 1);
    AssertEq(!!(a == c), 0);
    AssertEq(!!(a != b), 0);
    AssertEq(!!(a != c), 1);
    AssertEq(!!(k == b), 1);
    AssertEq(!!((struct Inner){2, "ab"} == c), 1);
}

[[cccc::test]] void test_prefixed_equals_binds_operators(void) {
    struct Pair a = {1, 2};
    struct Pair b = {1, 2};
    struct Pair c = {1, 3};
    AssertEq(!!(a == b), 1);
    AssertEq(!!(a != c), 1);
}
