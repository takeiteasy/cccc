// CCCC_FLAGS: -Wnonnull
// CCCC_EXPECT_STDERR: null value passed
// #689: every case (including default) nulls the pointer and breaks -- null
// on every live path, so this warns under plain -Wnonnull, not just
// -Wmaybe-nonnull.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int  y = 0;
    int *p = &y;
    int  k = 0;
    switch (k) {
        case 0:
            p = 0;
            break;
        case 1:
            p = 0;
            break;
        default:
            p = 0;
            break;
    }
    foo(p);
    return 42;
}
