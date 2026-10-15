// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct T \{\n    int a;\n    struct \{\n    int x;\n\};
// CCCC_REJECT_STDOUT: struct __cccc_anon[0-9]+;
//
// A C11 anonymous member has no declarator, so it stays inline even when a
// tagless global of the same shape exists.

struct T {
    int a;
    struct {
        int x;
    };
};

static struct {
    int x;
} g;

int main(void) {
    struct T t = {1};
    t.x        = 40;
    g.x        = 1;
    return t.a + t.x + g.x;
}
