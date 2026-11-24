// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: unknown attribute 'foo::bar' ignored
// CCCC_EXPECT_STDERR: 'cold' is not a 'clang' attribute; write \[\[gnu::cold\]\]
// CCCC_EXPECT_STDERR: 'aligned' is not a 'clang' attribute; write \[\[gnu::aligned\]\]
// CCCC_EXPECT_STDERR: 'packed' is not a 'acme' attribute; write \[\[gnu::packed\]\]
// A known name in another vendor's namespace has no effect; an unknown name is
// a warning, not an error.

[[foo::bar]] int x;
[[clang::cold]] int f(void) {
    return 1;
}
[[clang::aligned(16)]] int y;
struct [[acme::packed]] P {
    char c;
    int  i;
};

int main(void) {
    return sizeof(struct P) == 8 && _Alignof(y) == 4 ? 42 : 1;
}
