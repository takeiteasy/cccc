// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @generate_getters takes at most one argument

#include <cccc/derive.h>

@generate_getters("A", "B") struct S {
    int x;
};

int main(void) {
    return 42;
}
