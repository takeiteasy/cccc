// CCCC_FLAGS: -Wmain
// CCCC_EXPECT_STDERR: first parameter of 'main' should be 'int'.*\[-Wmain\]

int main(float x, char **argv) {
    (void)x;
    (void)argv;
    return 42;
}
