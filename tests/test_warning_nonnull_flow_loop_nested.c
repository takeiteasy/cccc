// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: may be null
// #689: three levels of nested loop fixpoints must still converge (a
// termination/no-hang smoke test for the bounded back-edge fixpoint) and
// correctly propagate the innermost conditional assignment out to the
// outermost exit.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int  x = 0;
    int *p = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                if (i == 1 && j == 1 && k == 1)
                    p = &x;
            }
        }
    }
    foo(p);
    return 42;
}
