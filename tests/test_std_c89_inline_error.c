/* EXPECT_COMPILE_ERROR */
/* CCCC_FLAGS: --std=c89 */
/* CCCC_EXPECT_STDERR: 'inline' is not available before C99 */
static inline int add(int a, int b) { return a + b; }
