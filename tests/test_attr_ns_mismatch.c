// CCCC_EXPECT_STDERR: 'packed' is not a 'cccc' attribute; write \[\[gnu::packed\]\]
// CCCC_EXPECT_STDERR: 'packed' is not a 'acme' attribute; write \[\[gnu::packed\]\]
// CCCC_EXPECT_STDERR: 'aligned' is not a standard attribute; write \[\[gnu::aligned\]\]
// CCCC_EXPECT_STDERR: 'single' is not a 'gnu' attribute; write \[\[cccc::single\]\]
// CCCC_EXPECT_STDERR: 'nodiscard' is not a 'clang' attribute; write \[\[nodiscard\]\]
// CCCC_EXPECT_STDERR: 'overloadable' is not a 'gnu' attribute; write \[\[clang::overloadable\]\]
// A known attribute written in a namespace it lacks is ignored with a warning,
// on by default.

struct [[cccc::packed]] A {
    char c;
    int  i;
};
struct [[acme::packed]] B {
    char c;
    int  i;
};
int y [[aligned(16)]];
int *[[gnu::single]] p;
[[clang::nodiscard]] int f(void);
[[gnu::overloadable]] int g(int);

int main(void) {
    return sizeof(struct A) == 8 && sizeof(struct B) == 8 && _Alignof(y) == 4
               ? 42
               : 1;
}
