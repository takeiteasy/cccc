/* EXPECT_COMPILE_ERROR */
/* JCC_FLAGS: -std=c89 */
/* JCC_EXPECT_STDERR: compound literals are not available before C99 */
int f(void) {
    int *p = (int []){1, 2, 3};
    return p[0];
}
