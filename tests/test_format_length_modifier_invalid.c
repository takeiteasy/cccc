// CCCC_FLAGS: --format-string-checks
// CCCC_EXPECT_STDERR: expected type 'long'
// CCCC_EXPECT_STDERR: expected type 'unsigned long'
// CCCC_EXPECT_STDERR: expected type 'long double'
/*
 * Test format string validation - invalid length modifier usage
 * Each call should emit a -Wformat warning for the wrong argument type.
 */

#include "stdio.h"

int main() {
    // %ld expects long, but int is provided
    printf("%ld\n", 42);

    // %zu expects unsigned long, but int is provided
    printf("%zu\n", 42);

    // %Lf expects long double, but double is provided
    printf("%Lf\n", 3.14);

    return 42;
}
