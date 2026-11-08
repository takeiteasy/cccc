// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: 'Q' does not match the function's return type

typedef struct {
    int x;
} P;
typedef struct {
    int x;
} Q;

[[cccc::test(return = (Q){.x = 1})]] P test_wrong_typedef(void) {
    return (P){1};
}
