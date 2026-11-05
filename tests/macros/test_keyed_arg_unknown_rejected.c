// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no parameter named 'z'

#include <cccc/derive.h>

int f(int x @ default(1));

int main(void) {
    f(.z = 1);
    return 42;
}
