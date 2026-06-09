// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --overflow-checks
// Test integer overflow detection for multiplication
// This test should trigger an overflow error when run with --overflow-checks flag
// Expected: Program aborts with overflow error message

#include "limits.h"

int main() {
    // This will overflow: LLONG_MAX * 2
    long long x = LLONG_MAX;
    long long result = x * 2;  // Overflow!

    // Should never reach here with overflow checks enabled
    return 42;
}
