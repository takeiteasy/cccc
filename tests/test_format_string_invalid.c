// CCCC_FLAGS: --format-string-checks
// CCCC_EXPECT_STDERR: format expects 2, call provides 1
/*
 * Test format string validation - invalid format strings
 * This test should emit a warning with -F/--format-string-checks enabled
 * It should detect a format string mismatch
 */

#include "stdio.h"

int main() {
    printf("Valid call first\n");

    // This should trigger format string mismatch error:
    // Format string expects 2 args, but we provide only 1
    printf("Missing argument: %d %d\n", 42);

    printf("This should not be reached\n");
    return 42;
}
