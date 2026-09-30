// CCCC_FLAGS: -I./tests/include_next/a -I./tests/include_next/b -I./tests/include_next/c
#include <chain.h>

int main(void) {
    if (CHAIN_A + CHAIN_B + OTHER != 7)
        return 1;
    if (CHAIN_A_HAS_NEXT != 1)
        return 2;
    if (CHAIN_B_HAS_NEXT != 0)
        return 3;
    return 42;
}
