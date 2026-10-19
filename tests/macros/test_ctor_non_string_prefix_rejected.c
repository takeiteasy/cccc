// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @ctor expects a string literal prefix

#include <cccc/derive.h>

@ctor(7) struct S {
    int a;
};

int main(void) {
    return 42;
}
