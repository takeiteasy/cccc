// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -c=generated -c
// CCCC_EXPECT_STDERR: conflicting -c formats

int main(void) {
    return 42;
}
