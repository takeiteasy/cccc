// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @equals_skip applies to a struct member

#include <cccc/derive.h>

@equals_skip int g;

int main(void) {
    return 42;
}
