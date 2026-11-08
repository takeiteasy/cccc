// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: 'Pt' does not match the type of member 'x'

typedef struct {
    int x;
} Pt;

[[cccc::test(return = (Pt){.x = (Pt){.x = 1}})]] Pt test_typed_literal_in_scalar(
    void) {
    return (Pt){1};
}
