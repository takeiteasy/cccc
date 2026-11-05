// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no argument for parameter 'a'

#include <cccc/derive.h>

int f(int a, int b @ default(2));

int main(void) {
    f(.b = 4);
    return 42;
}
