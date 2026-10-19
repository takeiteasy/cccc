// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @ctor takes at most one argument

#include <cccc/derive.h>

@ctor("a", "b") struct S {
    int a;
};

int main(void) {
    return 42;
}
