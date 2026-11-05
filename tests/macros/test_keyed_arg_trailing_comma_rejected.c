// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: expected an expression
// CCCC_REJECT_STDERR: positional argument

#include <cccc/derive.h>

int f(int x @ default(1), int y @ default(2));

int main(void) {
    f(.x = 3, );
    return 42;
}
