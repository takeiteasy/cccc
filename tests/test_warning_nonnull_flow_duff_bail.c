// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_REJECT_STDERR: nonnull
// #689: Duff's device -- a case label reachable from inside a loop body
// without an intervening switch of its own. nn_precise_ok rejects the loop
// (falls back to the barrier scheme) rather than trying to model a jump
// straight into the middle of the fixpoint's body. Must compile and run to
// completion (the loop terminates via the bounded counter) without a false
// positive.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int  x = 0;
    int *p = &x;
    int  i = 0;
    switch (i) {
        case 0:
            while (i < 3) {
                p = 0;
                i++;
                case 1:
                    p = &x;
                    i++;
            }
    }
    foo(p);
    return 42;
}
