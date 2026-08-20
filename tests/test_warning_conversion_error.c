// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Werror=conversion
// CCCC_EXPECT_STDERR: error: implicit conversion loses integer
// precision.*\[-Wconversion\]

int main(void) {
    long big   = 100000L;
    int  small = big;
    return small;
}
