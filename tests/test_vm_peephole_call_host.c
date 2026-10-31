// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: CALLF[^:]*: SX4
// A host function's int result has unspecified upper bits, so the sign
// extension after the call stays.
#include <stdlib.h>

int twice(int n) {
    int p = abs(n);
    return p + p;
}

int main(void) {
    return twice(-3) == 6 ? 42 : 1;
}
