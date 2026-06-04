/* JCC_FLAGS: -std=c89 -Wpedantic */
/* JCC_EXPECT_STDERR: warning: '//' comments are a C99 extension \[-Wpedantic\] */
int main(void) {
    int x = 1; // this is a line comment
    return x == 1 ? 42 : 1;
}
