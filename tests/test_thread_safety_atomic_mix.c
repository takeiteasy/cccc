// CCCC_FLAGS: --thread-safety
// CCCC_EXPECT_STDERR: discards '_Atomic'
// Warn when a cast strips the _Atomic qualifier from a pointer type.
// Full runtime detection of atomic/non-atomic mixed access requires
// ASTR/ALDR opcodes (follow-up ticket).
#include <stdatomic.h>

_Atomic int x = 0;

int main(void) {
    int *p = (int *)&x;  // cast discards _Atomic — triggers warning
    *p = 42;
    return *p;
}
