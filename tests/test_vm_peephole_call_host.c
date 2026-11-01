// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: CALLF[^:]*: SX4
// A host function's int result has unspecified upper bits, so the sign
// extension after the call stays when a compare reads the whole register (a
// store of the int would not need it).
#include <stdlib.h>

int above(int n) {
    return abs(n) > 2;
}

int main(void) {
    return above(-3) == 1 && above(1) == 0 ? 42 : 1;
}
