// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: cannot add two pointers
int main(void) {
    int *p = 0;
    return p + p;
}
