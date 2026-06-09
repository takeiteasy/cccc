// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --overflow-checks
// Test integer overflow detection for addition
// This test should trigger an overflow error when run with --overflow-checks flag
// Expected: Program aborts with overflow error message

#include "limits.h"

int main() {
    // This will overflow: LLONG_MAX + 1
    long long x = LLONG_MAX;
    long long result = x + 1;  // Overflow!

    // Should never reach here with overflow checks enabled
    return 42;
}
