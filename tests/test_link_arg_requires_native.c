// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --link-arg=-lm
// CCCC_EXPECT_STDERR: --link-arg requires -c=native

int main(void) {
    return 42;
}
