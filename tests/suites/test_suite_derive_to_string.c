// CCCC_FLAGS: --testing
// Suite: struct @to_string output (same text as @dump), enum prefix forms,
// truncation and @owned content, on the VM and under -c=native.

#include <cccc/derive.h>
#include <stdio.h>
#include <string.h>

enum Color { RED, GREEN, BLUE = 7 };

@to_string("app") @from_string("app") enum Shade { LIGHT, DARK };

@to_string struct Inner {
    int           a;
    unsigned char b;
};

@to_string struct Rec {
    int          id;
    double       ratio;
    _Bool        flag;
    enum Color   color;
    enum Color   odd;
    char         name[4];
    int          v[2];
    struct Inner in;
    struct {
        short s;
    };
    int secret @dump_skip;
    int hidden @to_string_skip;
};

@dump @to_string struct Both {
    int keep;
    int log_only @dump_skip;
    int dump_only @to_string_skip;
};

@to_string("app") struct Pair {
    int a;
    int b;
};

@to_string typedef struct {
    int x;
} Anon;

@to_string struct Owner {
    char         *label @owned;
    int          *vals @owned("n");
    int           n;
    struct Inner *inner @owned;
};

[[cccc::test]] void test_to_string_struct(void) {
    struct Rec r = {.id     = -7,
                    .ratio  = 0.5,
                    .flag   = 1,
                    .color  = BLUE,
                    .odd    = (enum Color)3,
                    .name   = "abc",
                    .v      = {1, -2},
                    .in     = {5, 200},
                    .s      = -4,
                    .secret = 99,
                    .hidden = 98};
    char       buf[256];
    size_t     n = Rec_to_string(&r, buf, sizeof buf);
    AssertEq(strcmp(buf, "Rec { id = -7, ratio = 0.5, flag = true, "
                         "color = BLUE, odd = 3, name = \"abc\", "
                         "v = { 1, -2 }, in = Inner { a = 5, b = 200 }, "
                         "s = -4 }"),
             0);
    AssertEq(n, strlen(buf));
}

[[cccc::test]] void test_to_string_honours_both_skips_dump_only_its_own(void) {
    struct Both b = {1, 2, 3};
    char        buf[64];
    Both_to_string(&b, buf, sizeof buf);
    AssertEq(strcmp(buf, "Both { keep = 1 }"), 0);

    FILE *f = tmpfile();
    Both_dump(&b, f);
    rewind(f);
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n]   = 0;
    fclose(f);
    AssertEq(strcmp(buf, "Both { keep = 1, dump_only = 3 }"), 0);
}

[[cccc::test]] void test_to_string_matches_dump(void) {
    struct Inner in = {5, 200};
    char         s[64];
    Inner_to_string(&in, s, sizeof s);
    AssertEq(strcmp(s, "Inner { a = 5, b = 200 }"), 0);
}

[[cccc::test]] void test_to_string_truncates_and_reports_full_length(void) {
    struct Inner in = {5, 200};
    char         buf[8];
    size_t       n = Inner_to_string(&in, buf, sizeof buf);
    AssertEq(n, strlen("Inner { a = 5, b = 200 }"));
    AssertEq(strcmp(buf, "Inner {"), 0);
}

[[cccc::test]] void test_to_string_measures_with_no_buffer(void) {
    struct Inner in = {5, 200};
    AssertEq(Inner_to_string(&in, NULL, 0), strlen("Inner { a = 5, b = 200 }"));
}

[[cccc::test]] void test_to_string_prefix_and_typedef(void) {
    struct Pair p = {1, 2};
    char        buf[64];
    app_Pair_to_string(&p, buf, sizeof buf);
    AssertEq(strcmp(buf, "Pair { a = 1, b = 2 }"), 0);

    Anon a = {3};
    Anon_to_string(&a, buf, sizeof buf);
    AssertEq(strcmp(buf, "Anon { x = 3 }"), 0);
}

[[cccc::test]] void test_enum_prefix(void) {
    AssertEq(strcmp(app_Shade_to_string(DARK), "DARK"), 0);
    AssertEq((int)app_Shade_from_string("LIGHT"), (int)LIGHT);
}

[[cccc::test]] void test_to_string_owned_content(void) {
    int          vals[]  = {4, 5};
    struct Inner in      = {1, 2};
    char         label[] = "hi";
    struct Owner o       = {label, vals, 2, &in};
    char         buf[128];
    Owner_to_string(&o, buf, sizeof buf);
    AssertEq(strcmp(buf, "Owner { label = \"hi\", vals = { 4, 5 }, n = 2, "
                         "inner = Inner { a = 1, b = 2 } }"),
             0);

    struct Owner none = {0};
    Owner_to_string(&none, buf, sizeof buf);
    AssertEq(strcmp(buf, "Owner { label = NULL, vals = NULL, n = 0, "
                         "inner = NULL }"),
             0);
}
