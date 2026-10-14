// CCCC_FLAGS: -F./tests/fixtures/frameworks_next/a -F./tests/fixtures/frameworks_next/b
// #include_next in a framework header continues into the next -F directory.
#include <Chain/Chain.h>

int main(void) {
    if (CHAIN_A + CHAIN_B != 3)
        return 1;
    if (CHAIN_A_HAS_NEXT != 1)
        return 2;
    if (CHAIN_B_HAS_NEXT != 0)
        return 3;
    return 42;
}
