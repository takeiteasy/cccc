/* EXPECT_COMPILE_ERROR */
/* JCC_FLAGS: --std=c89 */
/* JCC_EXPECT_STDERR: '_Bool' is not available before C99 */
_Bool flag = 1;
