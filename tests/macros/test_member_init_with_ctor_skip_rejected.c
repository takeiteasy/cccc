// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @default and @ctor_skip cannot share a member

#include <cccc/derive.h>

@ctor struct S {
    int a @ctor_skip = 1;
};

int main(void) {
    return 42;
}
