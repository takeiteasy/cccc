// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: positional argument after a keyed argument

#include <cccc/derive.h>

int f(int x @ default(1), int y @ default(2));

int main(void) {
    f(.x = 1, 2);
    return 42;
}
