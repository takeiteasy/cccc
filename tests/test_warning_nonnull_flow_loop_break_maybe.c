// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: may be null
// #689: unlike the no-cond infinite loop in
// test_warning_nonnull_flow_loop_break.c, this loop has a real `cond`, so
// falling out normally (p left null by the last iteration) is a genuine
// second predecessor of the exit alongside the break (p non-null) --
// the two paths must join to MAYBE.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int  x = 0;
    int  c = 0;
    int *p = 0;
    for (int i = 0; i < 5; i++) {
        if (c) {
            p = &x;
            break;
        }
        p = 0;
    }
    foo(p);
    return 42;
}
