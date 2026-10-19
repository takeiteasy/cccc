// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @ctor_skip takes no arguments
#include <cccc/derive.h>

struct A {
    int x @ctor_skip(1);
};

int main(void) {
    return 42;
}
