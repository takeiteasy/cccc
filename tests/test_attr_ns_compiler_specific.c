// Namespaces one compiler has and the other lacks are accepted without a warning.
// CCCC_REJECT_STDERR: not a
// CCCC_REJECT_STDERR: -Wunknown-attributes
[[gnu::optimize("O2")]] int a(int x) {
    return x;
}
[[clang::always_inline]] int b(int x) {
    return x;
}
[[clang::noinline]] int c(int x) {
    return x;
}
[[gnu::unavailable]] int gone(void);
int main(void) {
    return a(b(c(42)));
}
