// EXPECT_RUNTIME_ERROR JCC_FLAGS: --overflow-checks
// Test integer overflow detection for subtraction
// This test should trigger an overflow error when run with --overflow-checks flag
// Expected: Program aborts with overflow error message

#include "limits.h"

int main() {
    // This will underflow: LLONG_MIN - 1
    long long x = LLONG_MIN;
    long long result = x - 1;  // Underflow!

    // Should never reach here with overflow checks enabled
    return 42;
}
