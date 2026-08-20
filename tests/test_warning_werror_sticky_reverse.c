// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Wno-error=sign-compare -Werror=sign-compare
// CCCC_EXPECT_STDERR: error:.*\[-Wsign-compare\]

int main(void) {
    int          x = -1;
    unsigned int y = 1u;
    return (x < y) ? 1 : 0;
}
