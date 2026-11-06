// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17 --compiler-family=clang
// CCCC_EXPECT_STDERR: ISO C requires a named parameter before '...'

int f(...);

int main(void) {
    return 42;
}
