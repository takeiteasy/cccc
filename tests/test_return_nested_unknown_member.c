// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: 'struct In' has no member named 'zz'

struct In {
    int a;
};
typedef struct {
    struct In t;
} S;

[[cccc::test(return = (S){.t = {.zz = 1}})]] S test_nested_unknown_member(void) {
    return (S){0};
}
