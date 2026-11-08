// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: 'Nope' is not a typedef name

typedef struct {
    int x;
} P;

[[cccc::test(return = (Nope){.x = 1})]] P test_unknown_typedef(void) {
    return (P){1};
}
