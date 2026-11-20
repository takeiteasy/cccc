// CCCC_FLAGS: -Wall
// CCCC_EXPECT_STDERR: assignment to 'b' in an initializer; did you mean the
// designator '\.b ='\?.*\[-Woverride-init\]

struct Foo {
    int a, b;
};

int main(void) {
    int        b   = 5;
    struct Foo foo = {.a = 1, b = 3};
    return foo.b == 3 && b == 3 ? 42 : 1;
}
