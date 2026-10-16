// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @serialize expects a struct/union target

#include <cccc/derive.h>

@serialize typedef int Number;

int main(void) {
    return 42;
}
