// CCCC_FLAGS: --std=c17 --compiler-family=gcc -Wpedantic
// CCCC_NATIVE_SKIP: the host compiler decides whether a leading '...' is accepted before C23 (clang rejects it)
// CCCC_EXPECT_STDERR: ISO C requires a named parameter before '...'

int f(...);

int main(void) {
    return 42;
}
