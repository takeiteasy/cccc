// JCC_FLAGS: --format-string-checks
// JCC_EXPECT_STDERR: format expects 1, call provides 2
/*
 * Test format string validation - extra arguments
 * This test should emit a warning with -F/--format-string-checks enabled
 * It should detect too many arguments for the format string
 */

#include "stdio.h"

int main() {
    printf("Valid call first\n");

    // This should trigger format string mismatch error:
    // Format string expects 1 arg, but we provide 2
    printf("Extra argument: %d\n", 42, 99);

    printf("This should not be reached\n");
    return 42;
}
