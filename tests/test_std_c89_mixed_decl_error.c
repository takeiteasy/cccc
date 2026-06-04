/* EXPECT_COMPILE_ERROR */
/* JCC_FLAGS: -std=c89 */
/* JCC_EXPECT_STDERR: mixing declarations and code is not available before C99 */
int f(void) {
    int x = 1;
    x = 2;
    int y = 3;
    return x + y;
}
