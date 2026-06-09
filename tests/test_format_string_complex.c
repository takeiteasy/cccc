// JCC_FLAGS: --format-string-checks
/*
 * Test format string validation - complex format strings
 * Tests edge cases like width specifiers with *, length modifiers, etc.
 */

#include "stdio.h"

int main() {
    // Test with * width specifier (consumes an argument)
    printf("Width from arg: %*d\n", 10, 42);  // * consumes one arg, %d consumes another

    // Test with * precision specifier (consumes an argument)
    printf("Precision from arg: %.*f\n", 2, 3.14159);  // .* consumes one arg, %f consumes another

    // Test with both * width and * precision
    printf("Both dynamic: %*.*f\n", 10, 2, 3.14159);  // Two *, then %f = 3 args total

    // Test with length modifiers (shouldn't affect count)
    printf("Long: %ld\n", 42L);
    printf("Long long: %lld\n", 42LL);
    printf("Size_t: %zu\n", (unsigned long)100);

    // Test mixed specifiers
    printf("Complex: %d %s %f %x %p\n", 42, "test", 3.14, 255, (void*)0x1234);

    printf("\n=== All complex format string tests passed ===\n");
    return 42;
}
