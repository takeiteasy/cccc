// EXPECT_COMPILE_ERROR
// JCC_FLAGS: -std=c99
// JCC_EXPECT_STDERR: anonymous structs/unions are not available before C11
struct Outer {
    int x;
    struct { int a; int b; };
};
