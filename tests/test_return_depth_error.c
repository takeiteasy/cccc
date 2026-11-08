// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: compound-literal return= assertion nested too deeply

struct FwD0 {
    int v;
};
struct FwD1 {
    struct FwD0 a;
};
struct FwD2 {
    struct FwD1 a;
};
struct FwD3 {
    struct FwD2 a;
};
struct FwD4 {
    struct FwD3 a;
};
struct FwD5 {
    struct FwD4 a;
};
struct FwD6 {
    struct FwD5 a;
};
struct FwD7 {
    struct FwD6 a;
};
struct FwD8 {
    struct FwD7 a;
};
struct FwD9 {
    struct FwD8 a;
};
[[cccc::test(
    return =
               (struct FwD9){
                   .a =
                       {.a = {.a = {.a = {.a = {.a = {.a = {.a = {.a = {.v = 1}}}}}}}}}})]] struct
    FwD9 test_nested_too_deep(void) {
    struct FwD9 r = {0};
    return r;
}
