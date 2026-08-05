// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_REJECT_STDERR: nonnull
// #689: a do-while body always runs at least once, so an unconditional
// assignment inside it must NOT be weakened to MAYBE by the pre-loop state
// (unlike a `for`/`while`, which can run zero times) -- see
// test_warning_nonnull_flow_loop_zero_trip.c for the contrasting case.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) { }
int main(void) {
    int x = 0;
    int *p = 0;
    int c = 0;
    do {
        p = &x;
    } while (c);
    foo(p);
    return 42;
}
