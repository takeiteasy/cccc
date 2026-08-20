// CCCC_FLAGS: -Wnonnull
// CCCC_REJECT_STDERR: nonnull
// #689: the same maybe-null loop pattern must never fire under plain
// -Wnonnull -- MAYBE only ever warns when -Wmaybe-nonnull is also passed.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int  x = 0;
    int *p = 0;
    for (int i = 0; i < 3; i++) {
        if (i == 1)
            p = &x;
    }
    foo(p);
    return 42;
}
