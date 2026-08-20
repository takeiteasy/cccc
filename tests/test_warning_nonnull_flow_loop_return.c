// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_REJECT_STDERR: nonnull
// #689: validates that ND_RETURN correctly marks its env unreachable
// (bottom) inside a loop fixpoint. The early return only happens when p is
// about to become null, so the call after the loop must never see that
// branch's null state join in -- without the dead-env marking, this would
// incorrectly report "may be null" (or worse).
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
void helper(void) {
    int  y = 0;
    int *p = &y;
    for (int i = 0; i < 3; i++) {
        if (i == 0) {
            p = 0;
            return;
        }
    }
    foo(p);
}
int main(void) {
    helper();
    return 42;
}
