// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a @default can only name an earlier parameter

#include <cccc/derive.h>

int b = 3;

int f(int a @ default(b), int b @ default(0));

int main(void) {
    return 42;
}
