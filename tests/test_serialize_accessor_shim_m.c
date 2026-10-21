// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: return stdout;
// CCCC_EXPECT_STDOUT: return &errno;
// -m output defines the accessor shims in use in terms of the real symbol.
#include <stdio.h>
#include <errno.h>
int main(void) {
    (void)stdout;
    return errno == 0 ? 42 : 1;
}
