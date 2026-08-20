// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: may be null
// CCCC_REJECT_STDERR: null value passed
// #689: fall-through from a case that nulls the pointer into a case that
// uses it must join against the switch's entry state (non-null here),
// producing MAYBE -- not a barrier reset, and not a definite NULL either
// (the entry state is a real predecessor via fall-through-free dispatch).
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int  x = 0;
    int *p = &x;
    int  k = 0;
    switch (k) {
        case 0:
            p = 0;
            // fallthrough
        case 1:
            foo(p);
            break;
        default:
            break;
    }
    return 42;
}
