// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: too few arguments

#include <cccc/derive.h>

int f(int x @ default(1));
int (*fp)(int) = f;

int main(void) {
    fp();
    return 42;
}
