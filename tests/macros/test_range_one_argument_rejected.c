// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @range expects two integer constants

#include <cccc/derive.h>

struct S {
    int a @range(1);
};

int main(void) {
    return 42;
}
