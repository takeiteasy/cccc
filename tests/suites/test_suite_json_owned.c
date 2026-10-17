// CCCC_FLAGS: --testing
// Suite: @to_json / @from_json on @owned members, run on the VM and under
// -c=native.

#include <cccc/derive.h>
#include <cccc/json.h>
#include <stdlib.h>
#include <string.h>

@destroy @to_json @from_json struct Item {
    int   id;
    char *tag @owned;
};

@destroy @to_json @from_json struct Rec {
    char        *name @owned;
    int          n;
    int         *vals @owned("n");
    struct Item *item @owned;
    long         m;
    struct Item *items @owned("m");
    long        *one @owned;
};

@destroy @to_json @from_json struct List {
    int          v;
    struct List *next @owned;
};

@destroy @to_json("app") @from_json("app") struct Plain {
    char *s @owned;
};

static char *dup_str(const char *s) {
    char *p = malloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

static char out[1024];

static const char *dump(const struct Rec *r) {
    Rec_to_json(r, out, sizeof out);
    return out;
}

[[cccc::test]] void test_write(void) {
    struct Rec r;
    memset(&r, 0, sizeof r);
    r.name        = dup_str("a\"b");
    r.n           = 2;
    r.vals        = malloc(2 * sizeof(int));
    r.vals[0]     = 1;
    r.vals[1]     = 2;
    r.item        = calloc(1, sizeof(struct Item));
    r.item->id    = 7;
    r.item->tag   = dup_str("t");
    r.m           = 1;
    r.items       = calloc(1, sizeof(struct Item));
    r.items[0].id = 9;
    AssertStrEq(dump(&r), "{\"name\":\"a\\\"b\",\"n\":2,\"vals\":[1,2],"
                          "\"item\":{\"id\":7,\"tag\":\"t\"},\"m\":1,"
                          "\"items\":[{\"id\":9,\"tag\":null}],\"one\":null}");
    Rec_destroy(&r);
}

[[cccc::test]] void test_null_and_empty(void) {
    struct Rec r;
    memset(&r, 0, sizeof r);
    AssertStrEq(dump(&r), "{\"name\":null,\"n\":0,\"vals\":null,\"item\":null,"
                          "\"m\":0,\"items\":null,\"one\":null}");
    r.vals = malloc(1);
    AssertTrue(strstr(dump(&r), "\"vals\":[]") != NULL);
    free(r.vals);
}

[[cccc::test]] void test_round_trip(void) {
    const char *json =
        "{\"name\":\"ann\\n\",\"n\":3,\"vals\":[4,5,6],"
        "\"item\":{\"id\":1,\"tag\":\"x\"},\"m\":5,"
        "\"items\":[{\"id\":1,\"tag\":\"a\"},{\"id\":2,\"tag\":null},"
        "{\"id\":3,\"tag\":\"c\"},{\"id\":4,\"tag\":\"d\"},"
        "{\"id\":5,\"tag\":\"e\"}],\"one\":42}";
    struct Rec r;
    AssertEq(Rec_from_json(&r, json), 0);
    AssertStrEq(r.name, "ann\n");
    AssertEq(r.n, 3);
    AssertEq(r.vals[2], 6);
    AssertStrEq(r.item->tag, "x");
    AssertEq(r.m, 5);
    AssertEq(r.items[4].id, 5);
    AssertTrue(r.items[1].tag == NULL);
    AssertStrEq(r.items[3].tag, "d");
    AssertEq(*r.one, 42);
    AssertStrEq(dump(&r), json);
    Rec_destroy(&r);
}

[[cccc::test]] void test_null_and_empty_read(void) {
    struct Rec r;
    AssertEq(Rec_from_json(&r, "{\"name\":null,\"n\":0,\"vals\":[],"
                               "\"item\":null,\"m\":0,\"items\":null,"
                               "\"one\":null}"),
             0);
    AssertTrue(r.name == NULL && r.item == NULL && r.items == NULL);
    AssertTrue(r.vals != NULL);
    AssertEq(r.n, 0);
    Rec_destroy(&r);
}

[[cccc::test]] void test_length_member_order(void) {
    struct Rec r;
    AssertEq(Rec_from_json(&r, "{\"vals\":[1,2],\"n\":2,\"name\":null,"
                               "\"item\":null,\"m\":0,\"items\":null,"
                               "\"one\":null}"),
             0);
    AssertEq(r.n, 2);
    Rec_destroy(&r);
}

[[cccc::test]] void test_length_must_agree(void) {
    struct Rec r;
    memset(&r, 0x5a, sizeof r);
    AssertEq(Rec_from_json(&r, "{\"name\":null,\"n\":3,\"vals\":[1,2],"
                               "\"item\":null,\"m\":0,\"items\":null,"
                               "\"one\":null}"),
             1);
    AssertEq(Rec_from_json(&r, "{\"name\":null,\"vals\":[1,2],\"n\":3,"
                               "\"item\":null,\"m\":0,\"items\":null,"
                               "\"one\":null}"),
             1);
    AssertEq(Rec_from_json(&r, "{\"name\":null,\"n\":1,\"vals\":null,"
                               "\"item\":null,\"m\":0,\"items\":null,"
                               "\"one\":null}"),
             1);
    AssertEq((int)(unsigned char)((char *)&r)[0], 0x5a);
}

[[cccc::test]] void test_failures_leave_out_alone(void) {
    struct Rec r;
    memset(&r, 0x5a, sizeof r);
    const char *bad[] = {
        "{\"name\":\"x\",\"n\":1,\"vals\":[1,",
        "{\"name\":\"x\",\"n\":1,\"vals\":[\"s\"],\"item\":null}",
        "{\"name\":\"x\",\"name\":\"y\",\"n\":0,\"vals\":null,\"item\":null,"
        "\"m\":0,\"items\":null,\"one\":null}",
        "{\"name\":\"x\",\"n\":0,\"vals\":null,\"item\":{\"id\":1},"
        "\"m\":0,\"items\":null,\"one\":null}",
        "{\"name\":\"x\",\"n\":0,\"vals\":null,\"item\":null,\"m\":0,"
        "\"items\":null,\"one\":null} trailing",
        "{\"name\":3,\"n\":0}",
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        AssertEq(Rec_from_json(&r, bad[i]), 1);
    AssertEq((int)(unsigned char)((char *)&r)[0], 0x5a);
}

[[cccc::test]] void test_recursive_list(void) {
    struct List l;
    AssertEq(List_from_json(&l, "{\"v\":1,\"next\":{\"v\":2,\"next\":"
                                "{\"v\":3,\"next\":null}}}"),
             0);
    AssertEq(l.next->next->v, 3);
    char buf[128];
    List_to_json(&l, buf, sizeof buf);
    AssertStrEq(buf, "{\"v\":1,\"next\":{\"v\":2,\"next\":{\"v\":3,"
                     "\"next\":null}}}");
    List_destroy(&l);
}

[[cccc::test]] void test_prefix(void) {
    struct Plain p;
    AssertEq(app_Plain_from_json(&p, "{\"s\":\"hi\"}"), 0);
    char buf[32];
    app_Plain_to_json(&p, buf, sizeof buf);
    AssertStrEq(buf, "{\"s\":\"hi\"}");
    Plain_destroy(&p);
}
