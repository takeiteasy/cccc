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
