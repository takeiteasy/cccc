/* EXPECT_COMPILE_ERROR */
/* JCC_FLAGS: -std=c89 */
/* JCC_EXPECT_STDERR: flexible array members are not available before C99 */
struct S {
    int len;
    int data[];
};
