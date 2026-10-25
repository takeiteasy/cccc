// CCCC_FLAGS: --testing
// Suite: the derives on an @owned member whose pointee struct is declared
// later, stepped through with T_clone_size.

#include <cccc/derive.h>
#include <cccc/json.h>
#include <cccc/text.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Late;

@dump @to_json @from_json @to_string @from_string @serialize @deserialize
    @equals @hash @compare @clone @destroy struct Early {
    int          n;
    struct Late *one @owned;
    struct Late *many @owned("n");
};

@dump @to_json @from_json @to_string @from_string @serialize @deserialize
    @equals @hash @compare @clone @destroy struct Late {
    int   v;
    char *s @owned;
};

[[cccc::test]] void test_json_forward_pointee(void) {
    struct Early e;
    memset(&e, 0, sizeof e);
    const char *js =
        "{\"n\":2,\"one\":{\"v\":7,\"s\":\"a\"},\"many\":[{\"v\":1,"
        "\"s\":\"x\"},{\"v\":2,\"s\":null}]}";
    AssertEq(Early_from_json(&e, js), 0);
    AssertEq(e.n, 2);
    AssertEq(((struct Late *)e.one)->v, 7);
    AssertStrEq(((struct Late *)e.one)->s, "a");
    AssertEq(((struct Late *)e.many)[1].v, 2);
    AssertStrEq(((struct Late *)e.many)[0].s, "x");
    Early_destroy(&e);
}

[[cccc::test]] void test_text_forward_pointee(void) {
    struct Early e;
    memset(&e, 0, sizeof e);
    const char *tx = "Early { n = 2, one = Late { v = 7, s = \"a\" }, many = { "
                     "Late { v = 1, s = \"x\" }, Late { v = 2, s = NULL } } }";
    AssertEq(Early_from_string(&e, tx), 0);
    AssertEq(((struct Late *)e.one)->v, 7);
    AssertEq(((struct Late *)e.many)[1].v, 2);
    AssertStrEq(((struct Late *)e.many)[0].s, "x");
    Early_destroy(&e);
}

[[cccc::test]] void test_json_write_round_trip(void) {
    struct Early e;
    memset(&e, 0, sizeof e);
    const char *js =
        "{\"n\":2,\"one\":{\"v\":7,\"s\":\"a\"},\"many\":[{\"v\":1,"
        "\"s\":null},{\"v\":2,\"s\":\"q\"}]}";
    AssertEq(Early_from_json(&e, js), 0);
    char buf[256];
    Early_to_json(&e, buf, sizeof buf);
    AssertStrEq(buf, js);
    Early_destroy(&e);
}

[[cccc::test]] void test_text_write_round_trip(void) {
    struct Early e;
    memset(&e, 0, sizeof e);
    const char *tx = "Early { n = 2, one = Late { v = 7, s = \"a\" }, many = { "
                     "Late { v = 1, s = NULL }, Late { v = 2, s = \"q\" } } }";
    AssertEq(Early_from_string(&e, tx), 0);
    char buf[256];
    Early_to_string(&e, buf, sizeof buf);
    AssertStrEq(buf, tx);
    Early_destroy(&e);
}

[[cccc::test]] void test_dump_matches_to_string(void) {
    struct Early e;
    memset(&e, 0, sizeof e);
    AssertEq(Early_from_string(&e, "Early { n = 1, one = NULL, many = { Late { "
                                   "v = 3, s = NULL } } }"),
             0);
    char  buf[256], out[256];
    FILE *f = tmpfile();
    Early_dump(&e, f);
    rewind(f);
    size_t n = fread(out, 1, sizeof out - 1, f);
    out[n]   = 0;
    fclose(f);
    Early_to_string(&e, buf, sizeof buf);
    AssertStrEq(buf, out);
    Early_destroy(&e);
}

[[cccc::test]] void test_binary_round_trip(void) {
    struct Early e;
    memset(&e, 0, sizeof e);
    AssertEq(Early_from_string(
                 &e, "Early { n = 2, one = Late { v = 7, s = \"a\" }, many = { "
                     "Late { v = 1, s = NULL }, Late { v = 2, s = \"q\" } } }"),
             0);
    unsigned long size = Early_serialize_size(&e);
    char         *buf  = malloc(size);
    AssertEq(Early_serialize(&e, buf), (int)size);
    struct Early r = Early_deserialize(buf);
    AssertEq(r.n, 2);
    AssertEq(((struct Late *)r.one)->v, 7);
    AssertStrEq(((struct Late *)r.many)[1].s, "q");
    AssertTrue(((struct Late *)r.many)[0].s == NULL);
    Early_destroy(&r);
    Early_destroy(&e);
    free(buf);
}

[[cccc::test]] void test_equals_hash_compare_arrays(void) {
    const char *tx =
        "Early { n = 2, one = NULL, many = { Late { v = 1, s = NULL }, "
        "Late { v = 2, s = \"q\" } } }";
    struct Early a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    AssertEq(Early_from_string(&a, tx), 0);
    AssertEq(Early_from_string(&b, tx), 0);
    AssertTrue(Early_equals(&a, &b));
    AssertEq(Early_hash(&a), Early_hash(&b));
    AssertEq(Early_compare(&a, &b), 0);
    ((struct Late *)b.many)[1].v = 3;
    AssertTrue(!Early_equals(&a, &b));
    AssertTrue(Early_compare(&a, &b) < 0);
    Early_destroy(&a);
    Early_destroy(&b);
}
