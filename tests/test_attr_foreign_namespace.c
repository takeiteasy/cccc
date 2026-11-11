// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: unknown attribute 'foo::bar' ignored
// CCCC_REJECT_STDERR: unknown attribute '(clang::cold|acme::packed|clang::aligned)'
// An attribute in any vendor namespace resolves by its name; an unknown name
// in an unknown namespace is a warning, not an error.

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
    return sizeof(struct P) == 5 && _Alignof(y) == 16 ? 42 : 1;
}
