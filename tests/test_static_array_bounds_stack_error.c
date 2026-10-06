// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -3
// Indexing exactly one past the end of a local array is reported at the
// access, not later at function return (or never, when it stays in-frame).
int main(int argc, char **argv) {
    (void)argv;
    int a[8];
    for (int i = 0; i < 8; i++)
        a[i] = i;
    int k = 8 + argc - 1;
    a[k]  = 1;
    return a[0] == 0 ? 42 : 1;
}
