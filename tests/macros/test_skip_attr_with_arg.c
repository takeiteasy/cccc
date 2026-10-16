// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @constructor_skip takes no arguments
#include <cccc/derive.h>

struct A {
    int x @constructor_skip(1);
};

int main(void) {
    return 42;
}
