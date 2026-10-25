// CCCC_FLAGS: --testing
// Suite: @serialize / @deserialize append the content of @owned members after
// the fixed members, on the VM and under -c=native.

#include <cccc/derive.h>
#include <stdlib.h>
#include <string.h>

@serialize @deserialize @destroy struct Inner {
    int   a;
    char *tag @owned;
};

@serialize @deserialize @destroy struct Outer {
    int           n;
    char         *name @owned;
    int          *vals @owned("n");
    struct Inner *one @owned;
    struct Inner *many @owned("n");
    struct Inner  fixed;
    long         *skipped @owned @serialize_skip;
};

@serialize @deserialize @destroy struct List {
    int          v;
    struct List *next @owned;
};

@serialize @deserialize struct Plain {
    int  a;
    char b;
};

static char *dup_str(const char *s) {
    char *p = malloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

[[cccc::test]] void test_round_trip(void) {
    struct Inner  one     = {1, dup_str("x")};
    struct Inner  many[2] = {{2, NULL}, {3, dup_str("zz")}};
    int           vals[2] = {5, 6};
    struct Outer  o       = {2,    dup_str("hello"),  vals, &one,
                             many, {9, dup_str("f")}, NULL};
    unsigned long size    = Outer_serialize_size(&o);
    char         *buf     = malloc(size);
    AssertEq(Outer_serialize(&o, buf), (int)size);
    struct Outer r = Outer_deserialize(buf);
    AssertEq(r.n, 2);
    AssertStrEq(r.name, "hello");
    AssertEq(r.vals[1], 6);
    AssertEq(r.one->a, 1);
    AssertStrEq(r.one->tag, "x");
    AssertTrue(r.many[0].tag == NULL);
    AssertStrEq(r.many[1].tag, "zz");
    AssertStrEq(r.fixed.tag, "f");
    AssertTrue(r.skipped == NULL);
    AssertTrue(r.name != o.name);
    Outer_destroy(&r);
    free(buf);
    free(o.name);
    free(one.tag);
    free(many[1].tag);
    free(o.fixed.tag);
}

[[cccc::test]] void test_null_members(void) {
    struct Outer o;
    memset(&o, 0, sizeof o);
    unsigned long size = Outer_serialize_size(&o);
    AssertEq(size, sizeof(struct Outer) + 4 + 1);
    char *buf = malloc(size);
    AssertEq(Outer_serialize(&o, buf), (int)size);
    struct Outer r = Outer_deserialize(buf);
    AssertTrue(r.name == NULL && r.vals == NULL && r.one == NULL);
    free(buf);
}

[[cccc::test]] void test_recursive_list(void) {
    struct List c = {3, NULL}, b = {2, &c}, a = {1, &b};
    char        buf[256];
    AssertEq(List_serialize_size(&a), 3 * sizeof(struct List) + 3);
    AssertEq(List_serialize(&a, buf), (int)List_serialize_size(&a));
    struct List r = List_deserialize(buf);
    AssertEq(r.next->next->v, 3);
    AssertTrue(r.next->next->next == NULL);
    List_destroy(&r);
}

[[cccc::test]] void test_plain_struct_unchanged(void) {
    struct Plain p = {7, 'q'};
    char         buf[sizeof p];
    AssertEq(Plain_serialize_size(&p), sizeof p);
    AssertEq(Plain_serialize(&p, buf), (int)sizeof p);
    AssertEq(Plain_deserialize(buf).a, 7);
}

[[cccc::test]] void test_deserialize_into_round_trip(void) {
    struct Inner one = {1, dup_str("x")};
    struct Outer o;
    memset(&o, 0, sizeof o);
    o.n                = 1;
    o.name             = dup_str("hi");
    o.one              = &one;
    o.fixed.tag        = dup_str("f");
    unsigned long size = Outer_serialize_size(&o);
    char         *buf  = malloc(size);
    Outer_serialize(&o, buf);
    struct Outer r;
    AssertEq(Outer_deserialize_into(&r, buf, size), 0);
    AssertStrEq(r.name, "hi");
    AssertStrEq(r.one->tag, "x");
    Outer_destroy(&r);
    free(buf);
    free(o.name);
    free(one.tag);
    free(o.fixed.tag);
}

[[cccc::test]] void test_deserialize_into_rejects_truncation(void) {
    struct Inner many[2] = {{2, dup_str("ab")}, {3, dup_str("cd")}};
    int          vals[2] = {5, 6};
    struct Outer o;
    memset(&o, 0, sizeof o);
    o.n                = 2;
    o.name             = dup_str("hello");
    o.vals             = vals;
    o.many             = many;
    unsigned long size = Outer_serialize_size(&o);
    char         *buf  = malloc(size);
    Outer_serialize(&o, buf);
    for (unsigned long len = 0; len < size; len++) {
        struct Outer r;
        memset(&r, 0x5a, sizeof r);
        AssertEq(Outer_deserialize_into(&r, buf, len), 1);
        AssertEq(r.n, 0x5a5a5a5a);
    }
    struct Outer r;
    AssertEq(Outer_deserialize_into(&r, buf, size), 0);
    Outer_destroy(&r);
    free(buf);
    free(o.name);
    free(many[0].tag);
    free(many[1].tag);
}

[[cccc::test]] void test_deserialize_into_rejects_hostile_count(void) {
    struct Outer o;
    memset(&o, 0, sizeof o);
    int val            = 1;
    o.n                = 1;
    o.name             = dup_str("x");
    o.vals             = &val;
    unsigned long size = Outer_serialize_size(&o);
    char         *buf  = malloc(size);
    Outer_serialize(&o, buf);
    int big = 0x40000000;
    memcpy(buf, &big, sizeof big);
    struct Outer r;
    AssertEq(Outer_deserialize_into(&r, buf, size), 1);
    free(buf);
    free(o.name);
}

[[cccc::test]] void test_deserialize_into_plain_struct(void) {
    struct Plain p = {7, 'q'}, r;
    char         buf[sizeof p];
    Plain_serialize(&p, buf);
    AssertEq(Plain_deserialize_into(&r, buf, sizeof buf - 1), 1);
    AssertEq(Plain_deserialize_into(&r, buf, sizeof buf), 0);
    AssertEq(r.a, 7);
}
