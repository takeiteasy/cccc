// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @owned applies to a pointer member

#include <cccc/derive.h>

struct S {
    int a @owned;
};

int main(void) {
    return 42;
}
