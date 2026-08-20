// EXPECT_RUNTIME_ERROR
// CCCC_EXPECT_STDERR: INTEGER OVERFLOW
// A bare key in #pragma cccc config(...) is shorthand for "key = true"

#pragma cccc config(overflow_checks)

#include "limits.h"

int main() {
    long long x      = LLONG_MAX;
    long long result = x + 1; // overflow, caught by config(overflow_checks)
    return 42;
}
