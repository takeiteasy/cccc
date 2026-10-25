// CCCC_FLAGS: --testing
// Suite: @from_json / @from_string on an @owned member whose pointee struct is
// declared later, read through T_clone_size.

#include <cccc/derive.h>
#include <cccc/json.h>
#include <cccc/text.h>
#include <stdlib.h>
#include <string.h>

struct Late;

@from_json @from_string @clone @destroy struct Early {
    int          n;
    struct Late *one @owned;
    struct Late *many @owned("n");
};

@to_json @from_json @to_string @from_string @clone @destroy struct Late {
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
