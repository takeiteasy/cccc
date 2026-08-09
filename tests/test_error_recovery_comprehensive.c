// EXPECT_COMPILE_ERROR
// Comprehensive error recovery test
// This file contains multiple syntax errors that should be collected
// and reported together, demonstrating Level 2 error recovery

int main() {
    // Error 1: Invalid expression (missing operand)
    int x = 1 +;

    // This should still be parsed after error recovery
    int y = 2;

    // Error 2: Missing closing bracket
    int arr[10];
    int z = arr[5;

    // This should still be parsed
    int w = 3;

    // Error 3: Missing colon in ternary
    int a = (y > 0) ? 1;

    // This should still be parsed
    int b = 4;

    // All non-error code should work fine
    return 42;
}
