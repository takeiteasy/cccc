// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @dump_skip takes no arguments

#include <cccc/derive.h>

struct S {
    int a @dump_skip(1);
};

int main(void) {
    return 42;
}
