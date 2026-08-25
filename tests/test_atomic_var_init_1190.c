// CCCC_FLAGS: --std=c11
// Expected return: 42
// #1190: ATOMIC_VAR_INIT was missing from include/stdatomic.h entirely --
// a C11/C17 program using it (C11 7.17.2p1) got an undefined-macro error,
// where a real C11/C17 compiler gives a working expansion. Fixed with a
// `(value)` expansion gated to __STDC_VERSION__ <= 201710L, matching how
// glibc's and clang's own <stdatomic.h> gate it. See
// test_atomic_var_init_c23_error_1190.c for the C23 side of the gate
// (removed there, same as a real host compiler).
#include <stdatomic.h>

int main(void) {
    atomic_int x = ATOMIC_VAR_INIT(5);
    if (atomic_load(&x) != 5)
        return 1;

    atomic_long y = ATOMIC_VAR_INIT(100L);
    if (atomic_load(&y) != 100L)
        return 2;

    return 42;
}
