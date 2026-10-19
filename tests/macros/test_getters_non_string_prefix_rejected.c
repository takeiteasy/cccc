// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @getters expects a string literal prefix

#include <cccc/derive.h>

@getters(42) struct S {
    int x;
};

int main(void) {
    return 42;
}
