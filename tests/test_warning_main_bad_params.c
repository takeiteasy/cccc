// CCCC_NATIVE_SKIP: the host cc rejects a non-int main parameter
// CCCC_FLAGS: -Wmain
// CCCC_EXPECT_STDERR: first parameter of 'main' should be 'int'.*\[-Wmain\]

int main(float x, char **argv) {
    (void)x;
    (void)argv;
    return 42;
}
