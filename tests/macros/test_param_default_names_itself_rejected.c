// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a @default cannot name its own parameter

#include <cccc/derive.h>

int a = 1;

int f(int a @ default(a));

int main(void) {
    return 42;
}
