// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: 'restrict' cannot qualify the return= literal type 'Pt'
// CCCC_REJECT_STDERR: not an integer, floating

struct P {
    int x;
};
struct Q {
    int x;
};
typedef struct P Pt;

[[cccc::test(return = (restrict Pt){.x = 1})]] struct P
    test_restrict_literal(void) {
    return (struct P){1};
}
