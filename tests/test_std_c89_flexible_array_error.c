/* EXPECT_COMPILE_ERROR */
/* CCCC_FLAGS: --std=c89 */
/* CCCC_EXPECT_STDERR: flexible array members are not available before C99 */
struct S {
    int len;
    int data[];
};
