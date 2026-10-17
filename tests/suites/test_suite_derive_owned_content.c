// CCCC_FLAGS: --testing
// Suite: @equals / @hash / @dump / @compare on @owned members, run on the VM
// and under -c=native.

#include <cccc/derive.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

@equals @hash @dump @compare struct Item {
    int   id;
    char *tag @owned;
};

@equals @hash @dump @compare struct Rec {
    char        *name @owned;
    int          n;
    int         *vals @owned("n");
    struct Item *item @owned;
    struct Item *items @owned("n");
    long        *one @owned;
};

@equals @hash @dump @compare struct List {
    int          v;
    struct List *next @owned;
};

struct Late;

@equals @hash @dump @compare struct Early {
    struct Late *late @owned;
};

@equals @hash @dump @compare struct Late {
    char *s @owned;
};

static char *dup_str(const char *s) {
    char *p = malloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

static struct Rec make(const char *name, int a, int b, const char *tag) {
    struct Rec r;
    memset(&r, 0, sizeof r);
    r.name         = dup_str(name);
    r.n            = 2;
    r.vals         = malloc(2 * sizeof(int));
    r.vals[0]      = a;
    r.vals[1]      = b;
    r.item         = calloc(1, sizeof(struct Item));
    r.item->tag    = dup_str(tag);
    r.items        = calloc(2, sizeof(struct Item));
    r.items[0].tag = dup_str(tag);
    r.items[1].id  = b;
    r.one          = malloc(sizeof(long));
    *r.one         = a;
    return r;
}

static void release(struct Rec *r) {
    free(r->name);
    free(r->vals);
    free(r->item->tag);
    free(r->item);
    free(r->items[0].tag);
    free(r->items);
    free(r->one);
}

[[cccc::test]] void test_equal_by_content_not_address(void) {
    struct Rec a = make("ann", 1, 2, "t");
    struct Rec b = make("ann", 1, 2, "t");
    AssertTrue(a.name != b.name);
    AssertTrue(Rec_equals(&a, &b));
    AssertEq(Rec_hash(&a), Rec_hash(&b));
    AssertEq(Rec_compare(&a, &b), 0);

    b.name[0] = 'b';
    AssertTrue(!Rec_equals(&a, &b));
    AssertTrue(Rec_compare(&a, &b) < 0);
    AssertTrue(Rec_hash(&a) != Rec_hash(&b));
    b.name[0] = 'a';

    b.vals[1] = 9;
    AssertTrue(!Rec_equals(&a, &b));
    AssertTrue(Rec_compare(&a, &b) < 0);
    b.vals[1]      = 2;

    b.item->tag[0] = 'u';
    AssertTrue(!Rec_equals(&a, &b));
    AssertTrue(Rec_compare(&b, &a) > 0);
    b.item->tag[0] = 't';

    b.items[1].id  = 5;
    AssertTrue(!Rec_equals(&a, &b));
    b.items[1].id = 2;

    *b.one        = 7;
    AssertTrue(!Rec_equals(&a, &b));
    AssertTrue(Rec_compare(&a, &b) < 0);
    *b.one = 1;

    AssertTrue(Rec_equals(&a, &b));
    release(&a);
    release(&b);
}

[[cccc::test]] void test_null_members(void) {
    struct Rec a      = make("ann", 1, 2, "t");
    struct Rec b      = make("ann", 1, 2, "t");
    char      *a_name = a.name;
    char      *b_name = b.name;
    b.name            = NULL;
    AssertTrue(!Rec_equals(&a, &b));
    AssertTrue(Rec_compare(&b, &a) < 0);
    AssertTrue(Rec_compare(&a, &b) > 0);
    a.name = NULL;
    AssertTrue(Rec_equals(&a, &b));
    AssertEq(Rec_hash(&a), Rec_hash(&b));
    AssertEq(Rec_compare(&a, &b), 0);
    a.name = a_name;
    b.name = b_name;
    release(&a);
    release(&b);
}

[[cccc::test]] void test_string_prefix_orders_first(void) {
    struct Item a = {1, dup_str("ab")};
    struct Item b = {1, dup_str("abc")};
    AssertTrue(!Item_equals(&a, &b));
    AssertTrue(Item_compare(&a, &b) < 0);
    AssertTrue(Item_compare(&b, &a) > 0);
    free(a.tag);
    free(b.tag);
}

[[cccc::test]] void test_recursive_list(void) {
    struct List c1 = {3, NULL}, b1 = {2, &c1}, a1 = {1, &b1};
    struct List c2 = {3, NULL}, b2 = {2, &c2}, a2 = {1, &b2};
    AssertTrue(List_equals(&a1, &a2));
    AssertEq(List_hash(&a1), List_hash(&a2));
    AssertEq(List_compare(&a1, &a2), 0);
    c2.v = 4;
    AssertTrue(!List_equals(&a1, &a2));
    AssertTrue(List_compare(&a1, &a2) < 0);
    b2.next = NULL;
    AssertTrue(List_compare(&a1, &a2) > 0);
}

[[cccc::test]] void test_forward_declared_pointee(void) {
    struct Late  l1 = {dup_str("x")}, l2 = {dup_str("x")};
    struct Early a = {&l1}, b = {&l2};
    AssertTrue(Early_equals(&a, &b));
    AssertEq(Early_hash(&a), Early_hash(&b));
    AssertEq(Early_compare(&a, &b), 0);
    l2.s[0] = 'y';
    AssertTrue(!Early_equals(&a, &b));
    AssertTrue(Early_compare(&a, &b) < 0);
    free(l1.s);
    free(l2.s);
}

[[cccc::test]] void test_dump(void) {
    struct Rec r = make("ann", 1, 2, "t");
    char       buf[512];
    FILE      *f = tmpfile();
    Rec_dump(&r, f);
    rewind(f);
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n]   = 0;
    fclose(f);
    AssertStrEq(buf, "Rec { name = \"ann\", n = 2, vals = { 1, 2 }, "
                     "item = Item { id = 0, tag = \"t\" }, "
                     "items = { Item { id = 0, tag = \"t\" }, "
                     "Item { id = 2, tag = NULL } }, one = 1 }");
    release(&r);
}

[[cccc::test]] void test_dump_null(void) {
    struct Rec r;
    memset(&r, 0, sizeof r);
    char  buf[512];
    FILE *f = tmpfile();
    Rec_dump(&r, f);
    rewind(f);
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n]   = 0;
    fclose(f);
    AssertStrEq(buf, "Rec { name = NULL, n = 0, vals = NULL, item = NULL, "
                     "items = NULL, one = NULL }");
}
