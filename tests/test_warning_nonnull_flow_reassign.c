// CCCC_FLAGS: -Wnonnull
// CCCC_REJECT_STDERR: nonnull
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int  x = 0;
    int *p = 0;
    p      = &x;
    foo(p);
    return 42;
}
