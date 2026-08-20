// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: may be null
// #689: with no `default`, the switch as a whole is skippable, so the
// pre-switch (null) entry state is itself a live predecessor of the exit
// alongside both cases' non-null assignments -- MAYBE.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int  x = 0, y = 0;
    int *p = 0;
    int  k = 0;
    switch (k) {
        case 0:
            p = &x;
            break;
        case 1:
            p = &y;
            break;
    }
    foo(p);
    return 42;
}
