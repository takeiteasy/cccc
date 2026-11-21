// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: test_method_diag_site.c:[0-9]+: +static Foo wrap\(int v\)
//
// A diagnostic on a method hoisted out of a struct body shows that method's
// line.

struct Box {
    int v;
    static Foo wrap(int v) {
        return (Foo){v};
    }
};
int main(void) {
    return 0;
}
