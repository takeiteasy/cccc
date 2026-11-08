// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a @default that names a parameter needs a call inside a function

#include <cccc/derive.h>

int f(int a, int n @ default(a));
int f(int a, int n) {
    return n;
}

int g = f(1);

int main(void) {
    return 42;
}
