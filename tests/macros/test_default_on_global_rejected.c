// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @default applies to a struct member

#include <cccc/derive.h>

int g @ default(1);

int main(void) {
    return 42;
}
