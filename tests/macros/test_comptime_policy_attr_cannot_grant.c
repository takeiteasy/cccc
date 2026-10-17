// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: --comptime-allow is only accepted on the command line
// CCCC_FLAGS: --testing

#include <stdio.h>

[[cccc::test(flags = "--comptime-allow=fs")]]
void granted(void) {}

int main(void) {
    return 42;
}
