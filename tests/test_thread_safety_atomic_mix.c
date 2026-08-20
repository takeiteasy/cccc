// CCCC_FLAGS: --thread-safety
// CCCC_EXPECT_STDERR: discards '_Atomic'
// Warn when a cast strips the _Atomic qualifier from a pointer type.
// Full runtime detection of atomic/non-atomic mixed access is implemented via
// ASTR/ALDR opcodes; see test_thread_safety_atomic_mix_runtime.c (#447).
#include <stdatomic.h>

_Atomic int x = 0;

int main(void) {
    int *p = (int *)&x; // cast discards _Atomic — triggers warning
    *p     = 42;
    return *p;
}
