// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Wimplicit-function-declaration
// CCCC_EXPECT_STDERR: undefined function: missing
int main(void) {
    return missing();
}
