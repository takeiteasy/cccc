// JCC_FLAGS: -F
/*
 * Test format string validation - valid format strings
 * This test should pass with -F/--format-string-checks enabled
 */

#include "stdio.h"

int main() {
    // Test valid printf calls
    printf("Hello, world!\n");
    printf("Number: %d\n", 42);
    printf("Two numbers: %d %d\n", 10, 20);
    printf("String: %s\n", "test");
    printf("Mixed: %d %s %f\n", 42, "hello", 3.14);

    // Test with %% (literal percent)
    printf("Percentage: 100%%\n");
    printf("Value: %d%%\n", 50);

    // Test with width and precision
    printf("Formatted: %10d\n", 42);
    printf("Precision: %.2f\n", 3.14159);
    printf("Both: %10.2f\n", 3.14159);

    // Test sprintf
    char buf[100];
    sprintf(buf, "sprintf test: %d", 123);
    printf("Result: %s\n", buf);

    sprintf(buf, "x=%d y=%d", 10, 20);
    printf("Result: %s\n", buf);

    // Test with no format specifiers
    printf("No specifiers here\n");

    printf("\n=== All format string validation tests passed ===\n");
    return 42;
}
