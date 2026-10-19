// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @default takes one default value expression

#include <cccc/derive.h>

struct S {
    int a @ default;
};

int main(void) {
    return 42;
}
