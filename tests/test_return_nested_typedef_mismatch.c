// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: 'B' does not match the type of member 'in'

typedef struct {
    int x;
} A;
typedef struct {
    int x;
} B;
typedef struct {
    A in;
} Outer;

[[cccc::test(return = (Outer){.in = (B){.x = 1}})]] Outer
    test_wrong_nested_typedef(void) {
    return (Outer){{1}};
}
