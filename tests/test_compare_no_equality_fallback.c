// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: invalid operands to ==
#include <cccc/derive.h>

@compare struct A {
    int x;
};

int main(void) {
    struct A a = {1}, b = {1};
    return a == b;
}
