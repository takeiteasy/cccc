// CCCC_FLAGS: -Wnonnull
// CCCC_REJECT_STDERR: nonnull
// Conservative design: a pointer that is only *maybe* null after a branch
// must not warn -- only provably-null values do. See #679.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) { }
int main(void) {
    int x = 0;
    int *p = 0;
    if (x == 0) p = &x;
    foo(p);
    return 42;
}
