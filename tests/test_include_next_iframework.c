// CCCC_NATIVE_SKIP: framework search paths do not reach the host cc (#1711)
// CCCC_FLAGS: -F./tests/fixtures/frameworks_next/a --iframework ./tests/fixtures/frameworks_next/b
// #include_next crosses from the -F list into the --iframework list.
#include <Chain/Chain.h>

int main(void) {
    if (CHAIN_A + CHAIN_B != 3)
        return 1;
    if (CHAIN_A_HAS_NEXT != 1 || CHAIN_B_HAS_NEXT != 0)
        return 2;
    return 42;
}
