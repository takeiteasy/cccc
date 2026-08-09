// EXPECT_COMPILE_ERROR
// Test error recovery and multi-error reporting
// This test demonstrates that the parser can collect multiple errors
// when run in error collection mode (enabled by default in main.c)

int main() {
    int x = 1;

    // Stray break statement (error 1) - already has recovery
    break;

    int y = 2;

    // Stray continue statement (error 2) - already has recovery
    continue;

    int z = 3;

    // This should still parse and return successfully despite the errors above
    return 42;
}
