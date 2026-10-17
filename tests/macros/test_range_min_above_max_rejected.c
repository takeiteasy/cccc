// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @range min is greater than max

#include <cccc/derive.h>

struct S {
    int a @range(5, 1);
};

int main(void) {
    return 42;
}
