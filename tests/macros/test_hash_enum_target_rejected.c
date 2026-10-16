// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @hash expects a struct/union target

#include <cccc/derive.h>

@hash enum Mode { OFF, ON };

int main(void) {
    return 42;
}
