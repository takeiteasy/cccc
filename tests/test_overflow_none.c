// CCCC_FLAGS: --overflow-checks
// Test that normal arithmetic works correctly with overflow checks enabled
// This test should complete successfully and return 42
// Expected: Returns 42 (no overflow detected)

int main() {
    // Normal arithmetic operations that don't overflow
    int a = 10 + 20;      // 30
    int b = 50 - 20;      // 30
    int c = 6 * 7;        // 42
    int d = 84 / 2;       // 42

    // Verify results
    if (a != 30) return 1;
    if (b != 30) return 2;
    if (c != 42) return 3;
    if (d != 42) return 4;

    // All checks passed
    return 42;
}
