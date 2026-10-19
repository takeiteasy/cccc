// CCCC_NATIVE_SKIP: the host cc rejects a non-int main parameter
// CCCC_FLAGS: -Wmain
// CCCC_EXPECT_STDERR: first parameter of 'main' should be 'int'.*\[-Wmain\]

long long main(long long argc, char **argv) {
    return argc || argv ? 42 : 1;
}
