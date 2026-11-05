// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: parameter attribute '@default' is already given on an earlier declaration

#include <cccc/derive.h>

int f(int a @ default(1));
int f(int a @ default(1));

int main(void) {
    return 42;
}
