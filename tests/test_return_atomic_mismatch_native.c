// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing=native
// CCCC_EXPECT_STDERR: 'struct Q' does not match the function's return type
// CCCC_REJECT_STDERR: not an integer, floating

struct P {
    int x;
};
struct Q {
    int x;
};
typedef struct P Pt;

[[cccc::test(return = (_Atomic struct Q){.x = 1})]] struct P
    test_atomic_mismatch(void) {
    return (struct P){1};
}
