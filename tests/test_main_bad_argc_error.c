// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: main's first parameter must be int

long long main(long long argc, char **argv) {
    return argc || argv ? 42 : 1;
}
