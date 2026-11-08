// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined variable 'q'

#include <cccc/derive.h>

int f(int (*cb)(int q), int b @ default(q));

int main(void) {
    return 42;
}
