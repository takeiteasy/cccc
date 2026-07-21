// CCCC_FLAGS: -Wnonnull
// CCCC_REJECT_STDERR: nonnull
// A statically-dead branch must not trigger a flow-sensitive nonnull
// warning -- see #679.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) { }
int main(void) {
    int *p = 0;
    if (0) {
        foo(p);
    }
    return 42;
}
