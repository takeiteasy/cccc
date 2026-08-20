// CCCC_FLAGS: -Wnonnull
// CCCC_REJECT_STDERR: nonnull
// #689: a switch containing a user goto/label bails to the barrier scheme
// (pushing no jump-target frame of its own), nested inside an otherwise
// precise loop. The switch's own `break` therefore can't find a matching
// frame on the jump-target stack -- this must be treated as a safe,
// in-place reset rather than as "control left the loop" (which would be
// the wrong target and could manufacture a false positive in the
// surrounding precise loop's fixpoint). Must compile and run cleanly
// either way.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int  x = 0;
    int *p = 0;
    int  k = 0;
    for (int i = 0; i < 3; i++) {
        switch (k) {
            case 0:
                goto lbl;
            lbl:
                break;
        }
        p = &x;
    }
    foo(p);
    return 42;
}
