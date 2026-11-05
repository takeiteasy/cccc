// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: parameter 'x' already has a positional argument

#include <cccc/derive.h>

int f(int x @ default(1), int y @ default(2));

int main(void) {
    f(1, .x = 2);
    return 42;
}
