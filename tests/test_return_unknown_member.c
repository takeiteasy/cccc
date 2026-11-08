// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: 'Pt' has no member named 'nope'

typedef struct {
    int x;
} Pt;

[[cccc::test(return = (Pt){.x = 1, .nope = 1})]] Pt test_unknown_member(void) {
    return (Pt){1};
}
