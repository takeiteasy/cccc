// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: parameter 'x' is given twice

#include <cccc/derive.h>

int f(int x @ default(1));

int main(void) {
    f(.x = 1, .x = 2);
    return 42;
}
