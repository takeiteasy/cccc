// CCCC_NATIVE_SKIP: framework search paths do not reach the host cc (#1711)
// CCCC_FLAGS: -I./tests/fixtures/frameworks_next/inc -F./tests/fixtures/frameworks_next/a -F./tests/fixtures/frameworks_next/b
// #include_next in an -I header falls through into the framework directories.
#include <Chain/Chain.h>

int main(void) {
    if (CHAIN_I + CHAIN_A + CHAIN_B != 7)
        return 1;
    if (CHAIN_I_HAS_NEXT != 1 || CHAIN_A_HAS_NEXT != 1)
        return 2;
    if (CHAIN_B_HAS_NEXT != 0)
        return 3;
    return 42;
}
