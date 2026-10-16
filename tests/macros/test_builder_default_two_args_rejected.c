// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @builder_default takes one default value expression

#include <cccc/derive.h>

struct S {
    int a @builder_default(1, 2);
};

int main(void) {
    return 42;
}
