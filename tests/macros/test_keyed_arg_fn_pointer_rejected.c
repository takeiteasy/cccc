// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: keyed arguments need a direct call to a function

#include <cccc/derive.h>

int f(int x @ default(1));
int (*fp)(int) = f;

int main(void) {
    fp(.x = 2);
    return 42;
}
