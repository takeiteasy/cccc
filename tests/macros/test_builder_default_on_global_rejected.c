// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @builder_default applies to a struct member

#include <cccc/derive.h>

int g @builder_default(1);

int main(void) {
    return 42;
}
