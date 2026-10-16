// CCCC_FLAGS: --testing
// Suite: @owned / @clone / @destroy, run on the VM and under -c=native.

#include <cccc/derive.h>
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s) {
    char *p = malloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

@clone @destroy struct Rec {
    char *name @owned;
    int   n;
    int  *vals @owned("n");
    char *view;
};

@clone @destroy struct Node {
    int          v;
    char        *label @owned;
    struct Node *next @owned;
};

@clone @destroy typedef struct {
    int   x;
    char *s @owned;
} Item;

@clone @destroy struct Bag {
    long         count;
    Item        *items @owned("count");
    Item        *boxed @owned;
    struct {
        char *anon @owned;
    };
    Item         fixed[2];
    struct Rec   inner;
};

@clone @destroy struct Tail {
    char *s @owned;
    Item *box @owned;
    long  n;
    Item *big @owned("n");
};

@clone("app") @destroy("app") struct Plain {
    int  a;
    int *p @owned;
};

[[cccc::test]] void test_clone_is_independent(void) {
    struct Rec a = {dup_str("ann"), 3, malloc(3 * sizeof(int)), NULL};
    a.vals[0] = 1;
    a.vals[1] = 2;
    a.vals[2] = 3;
    a.view    = a.name;

    struct Rec b;
    AssertEq(Rec_clone(&b, &a), 0);
    AssertEq(strcmp(b.name, "ann"), 0);
    AssertEq(b.n, 3);
    AssertEq(b.vals[2], 3);
    AssertEq(b.view == a.name, 1);
    AssertEq(b.name != a.name, 1);
    AssertEq(b.vals != a.vals, 1);

    a.name[0] = 'X';
    a.vals[1] = 99;
    AssertEq(strcmp(b.name, "ann"), 0);
    AssertEq(b.vals[1], 2);

    Rec_destroy(&a);
    Rec_destroy(&b);
}

[[cccc::test]] void test_destroy_nulls_and_is_repeatable(void) {
    struct Rec a = {dup_str("x"), 1, malloc(sizeof(int)), NULL};
    Rec_destroy(&a);
    AssertEq(a.name == NULL, 1);
    AssertEq(a.vals == NULL, 1);
    Rec_destroy(&a);
}

[[cccc::test]] void test_null_members_stay_null(void) {
    struct Rec a = {NULL, 0, NULL, NULL};
    struct Rec b;
    AssertEq(Rec_clone(&b, &a), 0);
    AssertEq(b.name == NULL, 1);
    AssertEq(b.vals == NULL, 1);
    Rec_destroy(&b);
}

[[cccc::test]] void test_linked_list(void) {
    struct Node *c = calloc(1, sizeof *c);
    c->v           = 3;
    c->label       = dup_str("c");
    struct Node *b = calloc(1, sizeof *b);
    b->v           = 2;
    b->label       = dup_str("b");
    b->next        = c;
    struct Node a  = {1, dup_str("a"), b};

    struct Node copy;
    AssertEq(Node_clone(&copy, &a), 0);
    AssertEq(copy.next != a.next, 1);
    AssertEq(copy.next->next != c, 1);
    AssertEq(copy.next->next->v, 3);
    AssertEq(strcmp(copy.next->next->label, "c"), 0);
    AssertEq(copy.next->next->next == NULL, 1);

    c->v = 30;
    AssertEq(copy.next->next->v, 3);

    Node_destroy(&a);
    Node_destroy(&copy);
    AssertEq(copy.next == NULL, 1);
}

[[cccc::test]] void test_nested_and_deep_members(void) {
    struct Bag a;
    memset(&a, 0, sizeof a);
    a.count = 2;
    a.items = calloc(2, sizeof(Item));
    a.items[0].x = 1;
    a.items[0].s = dup_str("i0");
    a.items[1].x = 2;
    a.items[1].s = dup_str("i1");
    a.boxed      = calloc(1, sizeof(Item));
    a.boxed->s   = dup_str("boxed");
    a.anon       = dup_str("anon");
    a.fixed[1].s = dup_str("fixed1");
    a.inner.name = dup_str("inner");
    a.inner.n    = 2;
    a.inner.vals = malloc(2 * sizeof(int));
    a.inner.vals[0] = 7;
    a.inner.vals[1] = 8;

    struct Bag b;
    AssertEq(Bag_clone(&b, &a), 0);
    AssertEq(b.items != a.items, 1);
    AssertEq(b.items[1].x, 2);
    AssertEq(strcmp(b.items[1].s, "i1"), 0);
    AssertEq(b.items[1].s != a.items[1].s, 1);
    AssertEq(strcmp(b.boxed->s, "boxed"), 0);
    AssertEq(strcmp(b.anon, "anon"), 0);
    AssertEq(b.fixed[0].s == NULL, 1);
    AssertEq(strcmp(b.fixed[1].s, "fixed1"), 0);
    AssertEq(b.fixed[1].s != a.fixed[1].s, 1);
    AssertEq(strcmp(b.inner.name, "inner"), 0);
    AssertEq(b.inner.vals[1], 8);
    AssertEq(b.inner.vals != a.inner.vals, 1);

    a.items[1].s[0] = 'Z';
    AssertEq(strcmp(b.items[1].s, "i1"), 0);

    Bag_destroy(&a);
    Bag_destroy(&b);
    AssertEq(b.items == NULL, 1);
    AssertEq(b.fixed[1].s == NULL, 1);
    AssertEq(b.inner.name == NULL, 1);
}

[[cccc::test]] void test_prefix(void) {
    struct Plain a = {5, malloc(sizeof(int))};
    *a.p           = 6;
    struct Plain b;
    AssertEq(app_Plain_clone(&b, &a), 0);
    AssertEq(*b.p, 6);
    AssertEq(b.p != a.p, 1);
    app_Plain_destroy(&a);
    app_Plain_destroy(&b);
}

[[cccc::test]] void test_failed_allocation_frees_and_leaves_out_alone(void) {
    struct Bag a;
    memset(&a, 0, sizeof a);
    a.anon       = dup_str("anon");
    a.boxed      = calloc(1, sizeof(Item));
    a.boxed->s   = dup_str("boxed");
    a.inner.name = dup_str("inner");
    a.count      = 1L << 60;
    a.items      = (Item *)a.anon;

    struct Bag b;
    memset(&b, 0x5a, sizeof b);
    AssertEq(Bag_clone(&b, &a), 1);
    AssertEq((int)(unsigned char)((char *)&b)[0], 0x5a);

    a.items = NULL;
    a.count = 0;
    Bag_destroy(&a);
}

[[cccc::test]] void test_failure_after_earlier_members_cloned(void) {
    struct Tail a;
    memset(&a, 0, sizeof a);
    a.s        = dup_str("s");
    a.box      = calloc(1, sizeof(Item));
    a.box->s   = dup_str("box");
    a.n        = 1L << 60;
    a.big      = a.box;

    struct Tail b;
    memset(&b, 0x5a, sizeof b);
    AssertEq(Tail_clone(&b, &a), 1);
    AssertEq((int)(unsigned char)((char *)&b)[0], 0x5a);

    a.big = NULL;
    a.n   = 0;
    Tail_destroy(&a);
}
