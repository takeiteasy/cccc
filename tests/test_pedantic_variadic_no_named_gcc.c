// CCCC_FLAGS: --std=c17 --compiler-family=gcc -Wpedantic
// CCCC_EXPECT_STDERR: ISO C requires a named parameter before '...'

int f(...);

int main(void) {
    return 42;
}
