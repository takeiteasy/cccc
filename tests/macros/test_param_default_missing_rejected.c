// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: too few arguments

#include <cccc/derive.h>

int f(int a, int b @ default(2));

int main(void) {
    f();
    return 42;
}
