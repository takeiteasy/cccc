// @dump output: enums by name, nested and anonymous structs, arrays, strings,
// pointers, @dump_skip and the name prefix.
// CCCC_EXPECT_STDOUT: ^Rec \{ id = -7, big = 18446744073709551615, ratio = 0\.5, flag = true, color = GREEN, odd = 9, name = "ann", v = \{ 1, 2 \}, in = Inner \{ a = 3, grid = \{ \{ 1, 2 \}, \{ 3, 4 \} \} \}, x = 10, nul = (\(nil\)|(0x)?0+) \}$
// CCCC_EXPECT_STDOUT: ^Empty \{\}$
// CCCC_EXPECT_STDOUT: ^Skip \{ keep = 1 \}$

#include <cccc/derive.h>
#include <stdio.h>

enum Color { RED, GREEN, BLUE };

@dump struct Inner {
    int a;
    int grid[2][2];
};

@dump struct Rec {
    int                id;
    unsigned long long big;
    double             ratio;
    _Bool              flag;
    enum Color         color;
    enum Color         odd;
    char               name[8];
    int                v[2];
    struct Inner       in;
    struct {
        int x;
    };
    int *nul;
    int  secret @dump_skip;
};

@dump("app") struct Empty {
};

@dump struct Skip {
    int keep;
    int hidden @dump_skip;
};

int main(void) {
    struct Rec r = {.id    = -7,
                    .big   = 18446744073709551615ULL,
                    .ratio = 0.5,
                    .flag  = 1,
                    .color = GREEN,
                    .odd   = (enum Color)9,
                    .name  = "ann",
                    .v     = {1, 2},
                    .in    = {3, {{1, 2}, {3, 4}}},
                    .x     = 10,
                    .nul   = 0};
    Rec_dump(&r, stdout);
    putchar('\n');
    struct Empty e = {};
    app_Empty_dump(&e, stdout);
    putchar('\n');
    struct Skip s = {1, 2};
    Skip_dump(&s, stdout);
    putchar('\n');
    return 42;
}
