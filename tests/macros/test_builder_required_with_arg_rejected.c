// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @builder_required takes no arguments

#include <cccc/derive.h>

struct S {
    int a @builder_required(1);
};

int main(void) {
    return 42;
}
