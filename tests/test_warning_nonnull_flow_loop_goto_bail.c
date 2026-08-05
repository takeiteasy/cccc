// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_REJECT_STDERR: nonnull
// #689: a user goto/label inside a loop body makes the fixpoint's
// back-edge model unsound to apply -- nn_precise_ok rejects the construct
// and it falls back to the original barrier scheme (reset to UNKNOWN),
// which never warns. Losing precision here is fine; a false positive is not.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) { }
int main(void) {
    int x = 0;
    int *p = 0;
    for (int i = 0; i < 3; i++) {
        if (i == 1) goto skip;
        p = &x;
    skip:;
    }
    foo(p);
    return 42;
}
