// EXPECT_RUNTIME_ERROR JCC_FLAGS: --overflow-checks
// Test signed division overflow detection
// This test should trigger an overflow error when run with --overflow-checks flag
// Expected: Program aborts with overflow error message (LLONG_MIN / -1)

#include "limits.h"

int main() {
    // This will overflow: LLONG_MIN / -1 = LLONG_MAX + 1 (which can't be represented)
    long long x = LLONG_MIN;
    long long result = x / -1;  // Signed division overflow!

    // Should never reach here with overflow checks enabled
    return 42;
}
