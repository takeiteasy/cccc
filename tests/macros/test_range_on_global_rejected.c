// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @range applies to a struct member

#include <cccc/derive.h>

int x @range(0, 1);

int main(void) {
    return 42;
}
