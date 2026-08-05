// CCCC_FLAGS: -Wnonnull
// CCCC_REJECT_STDERR: nonnull
// #689: the mandatory no-false-positive test for `break`. This loop has no
// `cond` at all (an infinite `for (;;)`) and can only ever exit via the
// break, which always assigns p a non-null value right before breaking --
// the break's env must be joined into the loop's exit correctly, and there
// must be no spurious "fell out normally" predecessor for a loop with no
// cond (see the exit_normal handling in nn_walk_loop_precise).
void foo(int *p) __attribute__((nonnull));
void foo(int *p) { }
int main(void) {
    int x = 0;
    int *p = 0;
    int n = 3;
    for (;;) {
        n--;
        if (n == 0) {
            p = &x;
            break;
        }
        p = 0;
    }
    foo(p);
    return 42;
}
