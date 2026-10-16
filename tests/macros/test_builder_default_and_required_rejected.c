// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @builder_default and @builder_required cannot share a member

#include <cccc/derive.h>

struct S {
    int a @builder_default(1) @builder_required;
};

int main(void) {
    return 42;
}
