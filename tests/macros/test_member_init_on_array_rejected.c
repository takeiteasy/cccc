// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @default does not apply to an array member

#include <cccc/derive.h>

@ctor struct S {
    int a[2] = {1, 2};
};

int main(void) {
    return 42;
}
