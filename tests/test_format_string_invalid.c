// CCCC_NATIVE_SKIP: --format-string-checks is VM-only
// CCCC_FLAGS: --format-string-checks
// CCCC_EXPECT_STDERR: format expects 2, call provides 1
/*
 * Test format string validation - invalid format strings
 * This test should emit a warning with --format-string-checks enabled
 * It should detect a format string mismatch
 */

#include "stdio.h"

int main() {
    printf("Valid call first\n");

    // Checked but never run: the missing argument is undefined to read.
    if (0) {
        // Format string expects 2 args, but we provide only 1
        printf("Missing argument: %d %d\n", 42);
    }

    return 42;
}
