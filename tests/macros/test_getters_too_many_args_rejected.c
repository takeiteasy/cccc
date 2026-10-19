// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @getters takes at most one argument

#include <cccc/derive.h>

@getters("A", "B") struct S {
    int x;
};

int main(void) {
    return 42;
}
