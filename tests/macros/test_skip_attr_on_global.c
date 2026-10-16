// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @accessor_skip applies to a struct member
#include <cccc/derive.h>

int g @accessor_skip;

int main(void) {
    return 42;
}
