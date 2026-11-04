// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @ctor expects a type or typedef target, not a variable: write 'struct S \{ \.\.\. \} @ctor g;'
#include <cccc/derive.h>

@ctor struct A {
    int x;
} g;

int main(void) {
    return 0;
}
