// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a @default cannot name another parameter

#include <cccc/derive.h>

int f(int a, int b @ default(a));

int main(void) {

    return 42;
}
