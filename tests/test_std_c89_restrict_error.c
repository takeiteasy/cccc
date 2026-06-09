/* EXPECT_COMPILE_ERROR */
/* CCCC_FLAGS: --std=c89 */
/* CCCC_EXPECT_STDERR: 'restrict' is not available before C99 */
int sum(int n, int * restrict a, int * restrict b);
