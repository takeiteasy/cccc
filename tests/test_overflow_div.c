// EXPECT_RUNTIME_ERROR JCC_FLAGS: --overflow-checks
// Test integer overflow detection for division
// This test should trigger a division by zero error when run with --overflow-checks flag
// Expected: Program aborts with division by zero error message

int main() {
    // This will cause division by zero
    int x = 42;
    int y = 0;
    int result = x / y;  // Division by zero!

    // Should never reach here with overflow checks enabled
    return 42;
}
