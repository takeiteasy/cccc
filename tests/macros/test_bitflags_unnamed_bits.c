// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @bitflags: constant 'RX' \(5\) has bits no single-bit constant names \(0x4\)

#include <cccc/derive.h>

@bitflags enum Perm { R = 1, W = 2, RX = 5 };

int main(void) {
    return 42;
}
