// CCCC_FLAGS: --testing
// Suite: @dump output, run on the VM and under -c=native.

#include <cccc/derive.h>
#include <stdio.h>
#include <string.h>

enum Color { RED, GREEN, BLUE = 7 };

@dump struct Inner {
    int           a;
    unsigned char b;
};

@dump struct Rec {
    int          id;
    long long    big;
    unsigned     u;
    double       ratio;
    _Bool        flag;
    enum Color   color;
    enum Color   odd;
    char         name[4];
    int          v[3];
    struct Inner in;
    struct {
        short s;
    };
    int secret @dump_skip;
};

@dump("app") struct Pair {
    int a;
    int b;
};

// Runs `dump` into a temporary file and returns what it wrote.
static const char *captured(char *buf, size_t cap, FILE *f) {
    rewind(f);
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n]   = 0;
    fclose(f);
    return buf;
}

[[cccc::test]] void test_dump_struct(void) {
    struct Rec r = {.id     = -7,
                    .big    = -9000000000LL,
                    .u      = 4000000000u,
                    .ratio  = 0.5,
                    .flag   = 1,
                    .color  = BLUE,
                    .odd    = (enum Color)3,
                    .name   = "abc",
                    .v      = {1, -2, 3},
                    .in     = {5, 200},
                    .s      = -4,
                    .secret = 99};
    char       big[512];
    FILE      *f = tmpfile();
    Rec_dump(&r, f);
    AssertEq(strcmp(captured(big, sizeof big, f),
                    "Rec { id = -7, big = -9000000000, u = 4000000000, "
                    "ratio = 0.5, flag = true, color = BLUE, odd = 3, "
                    "name = \"abc\", v = { 1, -2, 3 }, "
                    "in = Inner { a = 5, b = 200 }, s = -4 }"),
             0);
}

[[cccc::test]] void test_dump_string_fills_array(void) {
    struct Rec r;
    memset(&r, 0, sizeof r);
    memcpy(r.name, "wxyz", 4);
    char  big[512];
    FILE *f = tmpfile();
    Rec_dump(&r, f);
    AssertEq(strstr(captured(big, sizeof big, f), "name = \"wxyz\", v = ") !=
                 NULL,
             1);
}

[[cccc::test]] void test_dump_prefix(void) {
    struct Pair p = {1, 2};
    char        buf[64];
    FILE       *f = tmpfile();
    app_Pair_dump(&p, f);
    AssertEq(strcmp(captured(buf, sizeof buf, f), "Pair { a = 1, b = 2 }"), 0);
}
