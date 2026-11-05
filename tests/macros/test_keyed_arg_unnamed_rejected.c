// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no parameter named 'x'

#include <cccc/derive.h>

int f(int, int y @ default(2));

int main(void) {
    f(.x = 1);
    return 42;
}
