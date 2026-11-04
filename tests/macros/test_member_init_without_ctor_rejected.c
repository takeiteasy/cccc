// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: member initializer needs @ctor on struct 'S'

#include <cccc/derive.h>

struct S {
    int x = 1;
};

int main(void) {
    return 42;
}
