// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @bitflags: constant 'NEG' \(-1\) is negative

#include <cccc/derive.h>

@bitflags enum Perm { R = 1, NEG = -1 };

int main(void) {
    return 42;
}
