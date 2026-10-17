// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @range expects two integer constants

#include <cccc/derive.h>

int top = 5;

struct S {
    int a @range(0, top);
};

int main(void) {
    return 42;
}
