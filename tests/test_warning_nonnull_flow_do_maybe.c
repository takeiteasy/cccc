// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: may be null
// #689: a conditional null assignment inside a do-while body still merges
// against the pre-loop non-null state via the back edge, producing MAYBE.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int  x = 0;
    int *p = &x;
    int  c = 0;
    int  n = 3;
    do {
        if (c)
            p = 0;
        n--;
    } while (n > 0);
    foo(p);
    return 42;
}
