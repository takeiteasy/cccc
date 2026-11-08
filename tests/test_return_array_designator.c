// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: a member designator 'x' does not apply to member 'arr'

typedef struct {
    int arr[2];
} S;

[[cccc::test(return = (S){.arr = {.x = 1}})]] S test_array_designator(void) {
    return (S){0};
}
