// CCCC_FLAGS: -Wnonnull
// CCCC_REJECT_STDERR: nonnull
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int *bar(void) __attribute__((returns_nonnull));
int *bar(void) {
    static int x;
    return &x;
}
int main(void) {
    int x = 0;
    foo(&x);
    bar();
    return 42;
}
