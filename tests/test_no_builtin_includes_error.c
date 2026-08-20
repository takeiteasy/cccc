// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --use-system-headers --no-builtin-includes
// CCCC_EXPECT_STDERR: cannot open file
// --no-builtin-includes: non-owned std headers must NOT fall back to
// CCCC's ./include when no SDK dir is configured. The include must fail.
#include <stdio.h>
int main(void) {
    return 42;
}
