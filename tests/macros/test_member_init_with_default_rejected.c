// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a member initializer and @default cannot share a member

#include <cccc/derive.h>

@ctor struct S {
    int a @ default(2) = 1;
};

int main(void) {
    return 42;
}
