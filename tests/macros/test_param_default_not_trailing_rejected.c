// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a parameter after a @default parameter needs a @default

#include <cccc/derive.h>

int f(int a @ default(1), int b);

int main(void) {
    return 42;
}
