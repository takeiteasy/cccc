/* EXPECT_COMPILE_ERROR */
/* JCC_FLAGS: -std=c89 */
/* JCC_EXPECT_STDERR: variable-length arrays are not available before C99 */
int f(int n) {
    int arr[n];
    return arr[0];
}
