// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: the '_Atomic\(\.\.\.\)' specifier is not supported
// CCCC_REJECT_STDERR: not an integer, floating

struct P {
    int x;
};
struct Q {
    int x;
};
typedef struct P Pt;

[[cccc::test(return = (_Atomic(Pt)){.x = 1})]] struct P
    test_atomic_specifier(void) {
    return (struct P){1};
}
