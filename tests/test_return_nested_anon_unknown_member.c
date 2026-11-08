// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: member 'an' has no member named 'zz'

typedef struct {
    struct {
        int a;
    } an;
} S;

[[cccc::test(return = (S){.an = {.zz = 1}})]] S test_anon_unknown_member(void) {
    return (S){0};
}
