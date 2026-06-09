/* EXPECT_COMPILE_ERROR */
/* CCCC_FLAGS: --std=c89 */
/* CCCC_EXPECT_STDERR: '_Bool' is not available before C99 */
_Bool flag = 1;
