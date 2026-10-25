// CCCC_FLAGS: --testing
// Suite: struct @from_string reads back the @to_string text — scalars, enums,
// strings with escapes, arrays, nested and anonymous structs, @owned content,
// skipped members and rejected input — on the VM and under -c=native.

#include <cccc/derive.h>
#include <cccc/text.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum Color { RED, GREEN, BLUE = 7 };

@to_string @from_string @equals struct Inner {
    int           a;
    unsigned char b;
};

@to_string @from_string @equals struct Scalars {
    signed char        c;
    short              s;
    int                i;
    long long          ll;
    unsigned char      uc;
    unsigned short     us;
    unsigned int       ui;
    unsigned long long ull;
    float              f;
    double             d;
    long double        ld;
    _Bool              flag;
    enum Color         color;
    enum Color         odd;
    int                bits  : 5;
    unsigned           ubits : 3;
};

@to_string @from_string @equals struct Rec {
    int          id;
    char         name[8];
    int          v[3];
    int          grid[2][2];
    struct Inner in;
    struct {
        short s;
        char  tag;
    };
    int secret @dump_skip;
    int hidden @to_string_skip;
};

@clone @destroy @to_string @from_string @equals struct Owner {
    char         *label @owned;
    int          *vals @owned("n");
    int           n;
    struct Inner *inner @owned;
};

@clone @destroy @to_string @from_string struct Node {
    int          value;
    struct Node *next @owned;
};

@to_string("app") @from_string("app") struct Pair {
    int a;
    int b;
};

@to_string @from_string typedef struct {
    int x;
} Anon;

@to_string @from_string struct Empty {
};

static void round_trip_rec(const struct Rec *in, struct Rec *out) {
    char buf[512];
    Rec_to_string(in, buf, sizeof buf);
    memset(out, 0x5a, sizeof *out);
    AssertEq(Rec_from_string(out, buf), 0);
}

[[cccc::test]] void test_scalars_round_trip(void) {
    struct Scalars a = {.c     = -128,
                        .s     = -32768,
                        .i     = -2147483647 - 1,
                        .ll    = -9223372036854775807LL - 1,
                        .uc    = 255,
                        .us    = 65535,
                        .ui    = 4294967295u,
                        .ull   = 18446744073709551615ull,
                        .f     = 0.1f,
                        .d     = 0.1,
                        .ld    = 0.1L,
                        .flag  = 1,
                        .color = BLUE,
                        .odd   = (enum Color)3,
                        .bits  = -16,
                        .ubits = 7};
    char           buf[512];
    Scalars_to_string(&a, buf, sizeof buf);
    struct Scalars b;
    memset(&b, 0, sizeof b);
    AssertEq(Scalars_from_string(&b, buf), 0);
    AssertEq(Scalars_equals(&a, &b), 1);
    AssertEq(b.f == 0.1f, 1);
    AssertEq(b.d == 0.1, 1);
    AssertEq(b.ld == 0.1L, 1);
}

[[cccc::test]] void test_float_specials(void) {
    struct Scalars a, b;
    memset(&a, 0, sizeof a);
    a.d = INFINITY;
    a.f = -INFINITY;
    char buf[512];
    Scalars_to_string(&a, buf, sizeof buf);
    AssertEq(Scalars_from_string(&b, buf), 0);
    AssertEq(isinf(b.d) && b.d > 0, 1);
    AssertEq(isinf(b.f) && b.f < 0, 1);
    a.d = NAN;
    Scalars_to_string(&a, buf, sizeof buf);
    AssertEq(Scalars_from_string(&b, buf), 0);
    AssertEq(isnan(b.d), 1);
}

[[cccc::test]] void test_enum_by_name_or_number(void) {
    struct Scalars b;
    memset(&b, 0, sizeof b);
    char           buf[512];
    struct Scalars a = {.color = GREEN, .odd = (enum Color) - 4};
    Scalars_to_string(&a, buf, sizeof buf);
    AssertEq(strstr(buf, "color = GREEN") != NULL, 1);
    AssertEq(strstr(buf, "odd = 4294967292") != NULL, 1);
    AssertEq(Scalars_from_string(&b, buf), 0);
    AssertEq((int)b.color, (int)GREEN);
    AssertEq((unsigned)b.odd, 4294967292u);
}

[[cccc::test]] void test_strings_with_escapes(void) {
    struct Rec a;
    memset(&a, 0, sizeof a);
    memcpy(a.name, "a\"\\\n\t\r\x01\x7f", 8);
    struct Rec b;
    round_trip_rec(&a, &b);
    AssertEq(memcmp(a.name, b.name, 8), 0);
}

[[cccc::test]] void test_string_filling_the_array_has_no_nul(void) {
    struct Rec a;
    memset(&a, 0, sizeof a);
    memcpy(a.name, "12345678", 8);
    struct Rec b;
    round_trip_rec(&a, &b);
    AssertEq(memcmp(b.name, "12345678", 8), 0);
}

[[cccc::test]] void test_short_string_pads_with_zeros(void) {
    struct Rec a;
    memset(&a, 0, sizeof a);
    strcpy(a.name, "ab");
    struct Rec b;
    round_trip_rec(&a, &b);
    AssertEq(strcmp(b.name, "ab"), 0);
    AssertEq(b.name[7], 0);
}

[[cccc::test]] void test_arrays_nested_and_anonymous(void) {
    struct Rec a = {.id   = -9,
                    .name = "x",
                    .v    = {1, -2, 3},
                    .grid = {{1, 2}, {3, 4}},
                    .in   = {5, 200},
                    .s    = -4,
                    .tag  = 'q'};
    struct Rec b;
    round_trip_rec(&a, &b);
    AssertEq(b.id, -9);
    AssertEq(b.v[1], -2);
    AssertEq(b.grid[1][0], 3);
    AssertEq(b.in.b, 200);
    AssertEq(b.s, -4);
    AssertEq(b.tag, 'q');
}

[[cccc::test]] void test_skipped_members_are_zeroed(void) {
    struct Rec a;
    memset(&a, 0, sizeof a);
    a.secret = 11;
    a.hidden = 12;
    struct Rec b;
    round_trip_rec(&a, &b);
    AssertEq(b.secret, 0);
    AssertEq(b.hidden, 0);
}

[[cccc::test]] void test_members_in_any_order_and_labels_ignored(void) {
    struct Inner in;
    AssertEq(Inner_from_string(&in, "Inner { b = 9, a = -1 }"), 0);
    AssertEq(in.a, -1);
    AssertEq(in.b, 9);
    AssertEq(Inner_from_string(&in, "  Whatever{a=3,b=4}  "), 0);
    AssertEq(in.a, 3);
    AssertEq(Inner_from_string(&in, "{ a = 5, b = 6 }"), 0);
    AssertEq(in.b, 6);
}

[[cccc::test]] void test_prefix_typedef_and_empty(void) {
    struct Pair p = {1, 2}, q;
    char        buf[64];
    app_Pair_to_string(&p, buf, sizeof buf);
    AssertEq(app_Pair_from_string(&q, buf), 0);
    AssertEq(q.b, 2);

    Anon a;
    AssertEq(Anon_from_string(&a, "Anon { x = 3 }"), 0);
    AssertEq(a.x, 3);

    struct Empty e;
    AssertEq(Empty_from_string(&e, "Empty {}"), 0);
    AssertEq(Empty_from_string(&e, "Empty { a = 1 }"), 1);
}

[[cccc::test]] void test_rejected_input_leaves_out_untouched(void) {
    const char *bad[] = {
        "",
        "Inner",
        "Inner {",
        "Inner { a = 1 }",
        "Inner { a = 1, b = 2, a = 3 }",
        "Inner { a = 1, b = 2, c = 3 }",
        "Inner { a = 1, b = 256 }",
        "Inner { a = 99999999999, b = 1 }",
        "Inner { a = x, b = 1 }",
        "Inner { a = 1 b = 2 }",
        "Inner { a = 1, b = -1 }",
        "Inner { a = 1, b = 2 } trailing",
        "Inner { a = 1, b = 2, }",
        "Inner { a 1, b = 2 }",
    };
    for (int i = 0; i < (int)(sizeof bad / sizeof *bad); i++) {
        struct Inner in = {77, 88};
        AssertEq(Inner_from_string(&in, bad[i]), 1);
        AssertEq(in.a, 77);
        AssertEq(in.b, 88);
    }
}

[[cccc::test]] void test_rejected_strings_and_arrays(void) {
    struct Rec r;
    memset(&r, 0, sizeof r);
    const char *bad[] = {
        "Rec { id = 1, name = \"123456789\", v = { 1, 2, 3 }, grid = { { 1, 2 "
        "}, { 3, 4 } }, in = Inner { a = 1, b = 2 }, s = 0, tag = 0 }",
        "Rec { id = 1, name = \"a\\q\", v = { 1, 2, 3 }, grid = { { 1, 2 }, { "
        "3, 4 } }, in = Inner { a = 1, b = 2 }, s = 0, tag = 0 }",
        "Rec { id = 1, name = \"unterminated, v = { 1, 2, 3 } }",
        "Rec { id = 1, name = \"a\", v = { 1, 2 }, grid = { { 1, 2 }, { 3, 4 } "
        "}, in = Inner { a = 1, b = 2 }, s = 0, tag = 0 }",
        "Rec { id = 1, name = \"a\", v = { 1, 2, 3, 4 }, grid = { { 1, 2 }, { "
        "3, 4 } }, in = Inner { a = 1, b = 2 }, s = 0, tag = 0 }",
        "Rec { id = 1, name = \"a\\000\", v = { 1, 2, 3 }, grid = { { 1, 2 }, "
        "{ 3, 4 } }, in = Inner { a = 1, b = 2 }, s = 0, tag = 0 }",
    };
    for (int i = 0; i < (int)(sizeof bad / sizeof *bad); i++)
        AssertEq(Rec_from_string(&r, bad[i]), 1);
}

[[cccc::test]] void test_owned_round_trip(void) {
    int          vals[]  = {4, 5, 6};
    struct Inner in      = {1, 2};
    char         label[] = "hi \"there\"\n";
    struct Owner a       = {label, vals, 3, &in};
    char         buf[256];
    Owner_to_string(&a, buf, sizeof buf);
    struct Owner b;
    memset(&b, 0x5a, sizeof b);
    AssertEq(Owner_from_string(&b, buf), 0);
    AssertEq(Owner_equals(&a, &b), 1);
    AssertEq(b.label != label, 1);
    Owner_destroy(&b);
}

[[cccc::test]] void test_owned_null_and_empty(void) {
    struct Owner a = {0}, b;
    char         buf[256];
    Owner_to_string(&a, buf, sizeof buf);
    AssertEq(Owner_from_string(&b, buf), 0);
    AssertEq(b.label == NULL && b.vals == NULL && b.inner == NULL, 1);

    AssertEq(
        Owner_from_string(
            &b, "Owner { label = \"\", vals = {  }, n = 0, inner = NULL }"),
        0);
    AssertEq(b.label != NULL && b.label[0] == 0, 1);
    AssertEq(b.vals != NULL && b.n == 0, 1);
    Owner_destroy(&b);
}

[[cccc::test]] void test_owned_array_length_agrees_in_either_order(void) {
    struct Owner b;
    AssertEq(Owner_from_string(&b, "Owner { n = 2, vals = { 1, 2 }, label = "
                                   "NULL, inner = NULL }"),
             0);
    AssertEq(b.n, 2);
    Owner_destroy(&b);
    AssertEq(Owner_from_string(&b, "Owner { vals = { 1, 2 }, n = 3, label = "
                                   "NULL, inner = NULL }"),
             1);
    AssertEq(Owner_from_string(&b, "Owner { n = 3, vals = { 1, 2 }, label = "
                                   "NULL, inner = NULL }"),
             1);
}

[[cccc::test]] void test_owned_failure_releases_and_keeps_out(void) {
    struct Owner out = {0};
    out.n            = 99;
    AssertEq(Owner_from_string(&out, "Owner { label = \"x\", vals = { 1, 2 }, "
                                     "n = 2, inner = Inner { a = 1 } }"),
             1);
    AssertEq(out.n, 99);
    AssertEq(Owner_from_string(&out, "Owner { label = \"x\", label = \"y\", "
                                     "vals = NULL, n = 0, inner = NULL }"),
             1);
}

[[cccc::test]] void test_recursive_owned_list(void) {
    struct Node *list = NULL, c = {3, NULL}, b = {2, &c}, a = {1, &b};
    char         buf[256];
    Node_to_string(&a, buf, sizeof buf);
    struct Node out;
    AssertEq(Node_from_string(&out, buf), 0);
    AssertEq(out.value, 1);
    AssertEq(out.next->next->value, 3);
    AssertEq(out.next->next->next == NULL, 1);
    Node_destroy(&out);
    (void)list;
}
