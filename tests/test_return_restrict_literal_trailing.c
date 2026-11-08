// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: '__restrict' cannot qualify the return= literal type 'struct P'
// CCCC_REJECT_STDERR: not an integer, floating

struct P {
    int x;
};
struct Q {
    int x;
};
typedef struct P Pt;

[[cccc::test(return = (struct P __restrict){.x = 1})]] struct P
    test_restrict_trailing(void) {
    return (struct P){1};
}
