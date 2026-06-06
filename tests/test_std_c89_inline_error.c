/* EXPECT_COMPILE_ERROR */
/* JCC_FLAGS: --std=c89 */
/* JCC_EXPECT_STDERR: 'inline' is not available before C99 */
static inline int add(int a, int b) { return a + b; }
