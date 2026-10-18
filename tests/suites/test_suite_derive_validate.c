// CCCC_FLAGS: --testing
// Suite: @validate, @clamp and @range, run on the VM and under -c=native.

#include <cccc/derive.h>
#include <string.h>

enum { LIMIT = 50, WIDE = LIMIT * 2 };

typedef enum { LOW = 1, MID, HIGH } Level;

@validate @clamp struct Cfg {
    int           port @range(1, 65535);
    unsigned      n @range(0, LIMIT);
    int           delta @range(-WIDE, WIDE);
    Level         lvl @range(LOW, HIGH);
    int           bits  : 4 @range(-3, 3);
    unsigned      flags : 3 @range(0, 7);
    unsigned char byte @range(0, 255);
    long          big @range(-5000000000, 5000000000);
    char          name[8];
    int           free_form;
    struct {
        short inner @range(10, 20);
    };
};

static void fill(struct Cfg *c) {
    memset(c, 0, sizeof *c);
    c->port  = 80;
    c->n     = 10;
    c->delta = 0;
    c->lvl   = MID;
    c->bits  = 1;
    c->flags = 1;
    c->byte  = 200;
    c->big   = 4000000000;
    c->inner = 15;
}

[[cccc::test]] void test_in_range_is_valid(void) {
    struct Cfg c;
    fill(&c);
    AssertEq(Cfg_validate(&c), 1);
    c.free_form = 12345;
    AssertEq(Cfg_validate(&c), 1);
}

[[cccc::test]] void test_each_member_is_checked(void) {
    struct Cfg c;
    fill(&c);
    c.port = 0;
    AssertEq(Cfg_validate(&c), 0);
    fill(&c);
    c.port = 65536;
    AssertEq(Cfg_validate(&c), 0);
    fill(&c);
    c.n = 51;
    AssertEq(Cfg_validate(&c), 0);
    fill(&c);
    c.delta = -101;
    AssertEq(Cfg_validate(&c), 0);
    fill(&c);
    c.lvl = (Level)0;
    AssertEq(Cfg_validate(&c), 0);
    fill(&c);
    c.bits = -4;
    AssertEq(Cfg_validate(&c), 0);
    fill(&c);
    c.big = 5000000001;
    AssertEq(Cfg_validate(&c), 0);
    fill(&c);
    c.inner = 21;
    AssertEq(Cfg_validate(&c), 0);
}

[[cccc::test]] void test_bounds_are_inclusive(void) {
    struct Cfg c;
    fill(&c);
    c.port  = 1;
    c.n     = 50;
    c.delta = -100;
    c.lvl   = HIGH;
    c.bits  = -3;
    c.flags = 7;
    c.byte  = 255;
    c.inner = 10;
    AssertEq(Cfg_validate(&c), 1);
}

[[cccc::test]] void test_clamp_moves_to_the_nearer_bound(void) {
    struct Cfg c;
    fill(&c);
    c.port  = -9;
    c.n     = 900;
    c.delta = 101;
    c.lvl   = (Level)7;
    c.bits  = -4;
    c.big   = -6000000000;
    c.inner = 99;
    Cfg_clamp(&c);
    AssertEq(c.port, 1);
    AssertEq(c.n, 50u);
    AssertEq(c.delta, 100);
    AssertEq(c.lvl, HIGH);
    AssertEq(c.bits, -3);
    AssertEq(c.big, -5000000000);
    AssertEq(c.inner, 20);
    AssertEq(Cfg_validate(&c), 1);
}

[[cccc::test]] void test_clamp_leaves_valid_values(void) {
    struct Cfg a, b;
    fill(&a);
    fill(&b);
    a.free_form = b.free_form = -7;
    Cfg_clamp(&a);
    AssertEq(memcmp(&a, &b, sizeof a), 0);
}

@validate("app") @clamp("app") struct Pair {
    int lo @range(0, 9);
    int hi @range(0, 9);
};

[[cccc::test]] void test_prefix(void) {
    struct Pair p = {-1, 12};
    AssertEq(app_Pair_validate(&p), 0);
    app_Pair_clamp(&p);
    AssertEq(p.lo, 0);
    AssertEq(p.hi, 9);
    AssertEq(app_Pair_validate(&p), 1);
}

@validate typedef struct {
    unsigned long u @range(0, 100);
    long long     s @range(-9223372036854775807LL - 1, 5);
} Typed;

[[cccc::test]] void test_typedef_and_full_width(void) {
    Typed t = {101, 6};
    AssertEq(Typed_validate(&t), 0);
    t.u = 100;
    t.s = 6;
    AssertEq(Typed_validate(&t), 0);
    t.s = -9223372036854775807LL - 1;
    AssertEq(Typed_validate(&t), 1);
}

struct Unused {
    int x @range(1, 2);
};

[[cccc::test]] void test_range_without_a_generator(void) {
    struct Unused u = {5};
    AssertEq(u.x, 5);
}

typedef enum { COLD, WARM, HOT } Temp;

@validate @clamp struct Inner {
    int a @range(0, 9);
};

typedef struct Tdef Tdef;
@validate @clamp struct Tdef {
    short b @range(-2, 2);
};

struct Plain {
    int p;
};

@validate @clamp struct Deep {
    int          vals[3] @range(1, 5);
    short        grid[2][3] @range(0, 100);
    Temp         temps[2] @range(WARM, HOT);
    struct Inner one;
    struct Inner many[2];
    Tdef         td;
    struct Plain plain;
    struct {
        int anon[2] @range(0, 1);
    };
};

static void deep_fill(struct Deep *d) {
    memset(d, 0, sizeof *d);
    for (int i = 0; i < 3; i++)
        d->vals[i] = 3;
    d->temps[0] = WARM;
    d->temps[1] = HOT;
}

[[cccc::test]] void test_array_elements_are_checked(void) {
    struct Deep d;
    deep_fill(&d);
    AssertEq(Deep_validate(&d), 1);
    d.vals[2] = 6;
    AssertEq(Deep_validate(&d), 0);
    deep_fill(&d);
    d.grid[1][2] = 101;
    AssertEq(Deep_validate(&d), 0);
    deep_fill(&d);
    d.temps[0] = COLD;
    AssertEq(Deep_validate(&d), 0);
    deep_fill(&d);
    d.anon[1] = 2;
    AssertEq(Deep_validate(&d), 0);
}

[[cccc::test]] void test_array_elements_are_clamped(void) {
    struct Deep d;
    deep_fill(&d);
    d.vals[0]    = 0;
    d.vals[2]    = 99;
    d.grid[1][1] = -4;
    d.grid[0][2] = 500;
    d.temps[0]   = COLD;
    d.anon[0]    = -1;
    Deep_clamp(&d);
    AssertEq(d.vals[0], 1);
    AssertEq(d.vals[1], 3);
    AssertEq(d.vals[2], 5);
    AssertEq(d.grid[1][1], 0);
    AssertEq(d.grid[0][2], 100);
    AssertEq(d.temps[0], WARM);
    AssertEq(d.anon[0], 0);
    AssertEq(Deep_validate(&d), 1);
}

[[cccc::test]] void test_nested_structs_use_their_own_functions(void) {
    struct Deep d;
    deep_fill(&d);
    d.one.a = 10;
    AssertEq(Deep_validate(&d), 0);
    deep_fill(&d);
    d.many[1].a = -1;
    AssertEq(Deep_validate(&d), 0);
    deep_fill(&d);
    d.td.b = 3;
    AssertEq(Deep_validate(&d), 0);
    d.one.a     = 11;
    d.many[0].a = 12;
    d.many[1].a = -5;
    d.td.b      = -9;
    d.plain.p   = 1234;
    Deep_clamp(&d);
    AssertEq(d.one.a, 9);
    AssertEq(d.many[0].a, 9);
    AssertEq(d.many[1].a, 0);
    AssertEq(d.td.b, -2);
    AssertEq(d.plain.p, 1234);
    AssertEq(Deep_validate(&d), 1);
}

@validate @clamp struct Wide {
    unsigned long long all @range(0, 18446744073709551615ULL);
    unsigned long long hi
        @range(9223372036854775808ULL, 18446744073709551610ULL);
    unsigned long long arr[2] @range(5, 18446744073709551000ULL);
};

[[cccc::test]] void test_unsigned_64_bit_bounds(void) {
    struct Wide w = {~0ULL, 18446744073709551610ULL, {5, 6}};
    AssertEq(Wide_validate(&w), 1);
    w.hi = 18446744073709551611ULL;
    AssertEq(Wide_validate(&w), 0);
    w.hi = 9223372036854775807ULL;
    AssertEq(Wide_validate(&w), 0);
    w.arr[1] = 4;
    Wide_clamp(&w);
    AssertEq(w.hi, 9223372036854775808ULL);
    AssertEq(w.arr[1], 5ULL);
    w.arr[0] = ~0ULL;
    Wide_clamp(&w);
    AssertEq(w.arr[0], 18446744073709551000ULL);
    AssertEq(w.all, ~0ULL);
    AssertEq(Wide_validate(&w), 1);
}
