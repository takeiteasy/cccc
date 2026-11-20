// CCCC_FLAGS: -Wall
// CCCC_REJECT_STDERR: warning:

struct Foo {
    int a, b;
};

int main(void) {
    int        b = 5, c = 0;
    struct Foo foo = {.a = 1, (b = 3)};
    struct Foo bar = {c = 39, 0};
    return foo.b + bar.a;
}
