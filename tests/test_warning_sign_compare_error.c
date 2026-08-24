// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Werror=sign-compare
// CCCC_EXPECT_STDERR: error: comparison of integers with different signs.*\[-Wsign-compare\]

int main(void) {
    int          x = -1;
    unsigned int y = 1u;
    return (x < y) ? 1 : 0;
}
