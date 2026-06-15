// EXPECT_RUNTIME_ERROR
// CCCC_EXPECT_STDERR: INTEGER OVERFLOW
// #pragma cccc config(safety = 2) enables the STANDARD safety preset
// (which includes overflow checks) with no CLI safety flags.

#pragma cccc config(safety = 2)

#include "limits.h"

int main() {
    long long x = LLONG_MAX;
    long long result = x + 1; // overflow, caught by the STANDARD preset
    return 42;
}
