// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @bitflags: constant 'RW' \(3\) is not a power of two

#include <cccc/derive.h>

@bitflags enum Perm { R = 1, W = 2, RW = 3 };

int main(void) {
    return 42;
}
