// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --overflow-checks --optimize=3
// Checked arithmetic traps must survive constant folding.

#include "limits.h"

int main(void) {
    long long result = LLONG_MAX + 1LL;
    return result == 0 ? 1 : 42;
}
