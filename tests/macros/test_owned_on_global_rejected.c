// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @owned applies to a struct member

#include <cccc/derive.h>

int g @owned;

int main(void) {
    return 42;
}
