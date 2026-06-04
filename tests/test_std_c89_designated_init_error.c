/* EXPECT_COMPILE_ERROR */
/* JCC_FLAGS: -std=c89 */
/* JCC_EXPECT_STDERR: designated initializers are not available before C99 */
int arr[3] = { [1] = 42 };
