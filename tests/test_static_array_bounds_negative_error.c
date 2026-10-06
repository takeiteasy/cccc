// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -3
// A negative index before the start of a global array is reported.
static int g[8];

int main(int argc, char **argv) {
    (void)argv;
    int k = argc > 100 ? 0 : -1;
    g[k]  = 1;
    return 42;
}
