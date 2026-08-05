// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_REJECT_STDERR: nonnull
// #689: same as switch_no_default.c, but every path (including `default`)
// assigns a non-null value -- the switch can no longer be skipped with a
// null result, so the exit stays NONNULL and nothing warns.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) { }
int main(void) {
    int x = 0, y = 0, z = 0;
    int *p = 0;
    int k = 0;
    switch (k) {
    case 0:
        p = &x;
        break;
    case 1:
        p = &y;
        break;
    default:
        p = &z;
        break;
    }
    foo(p);
    return 42;
}
