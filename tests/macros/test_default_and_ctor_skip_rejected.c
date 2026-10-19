// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @default and @ctor_skip cannot share a member

#include <cccc/derive.h>

struct S {
    int a @ default(1) @ctor_skip;
};

int main(void) {
    return 42;
}
