// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: 'I' is not a struct or union type

typedef int I;
typedef struct {
    int x;
} P;

[[cccc::test(return = (I){.x = 1})]] P test_scalar_typedef(void) {
    return (P){1};
}
