// CCCC_REJECT_STDERR: warning:

struct Foo {
    int a, b;
};

int main(void) {
    int        b   = 39;
    struct Foo foo = {.a = 1, b = 3};
    return foo.a + b + 38;
}
