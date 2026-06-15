// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --overflow-checks
// CCCC_EXPECT_STDERR: INTEGER OVERFLOW
// CLI flags win over #pragma cccc config(...) (#357): the pragma tries to
// disable overflow checks, but --overflow-checks on the CLI takes priority,
// so the overflow below is still caught.

#pragma cccc config(overflow_checks = false)

#include "limits.h"

int main() {
    long long x = LLONG_MAX;
    long long result = x + 1;
    return 42;
}
