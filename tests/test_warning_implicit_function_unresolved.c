// EXPECT_COMPILE_ERROR
// JCC_FLAGS: -Wimplicit-function-declaration
// JCC_EXPECT_STDERR: undefined function: missing
int main(void) {
    return missing();
}
