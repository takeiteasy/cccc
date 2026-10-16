// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @builder_default does not apply to an array member

#include <cccc/derive.h>

struct S {
    int a[2] @builder_default(1);
};

int main(void) {
    return 42;
}
