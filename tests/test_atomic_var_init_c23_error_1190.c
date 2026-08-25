// EXPECT_COMPILE_ERROR
// #1190: ATOMIC_VAR_INIT is deprecated in C17 and removed entirely in C23
// (Annex 4 deprecation, then removal) -- cccc defaults to C23
// (CCCC_STD_C23), so the macro must NOT be defined without an explicit
// --std=c11/c17, matching what a real C23 compiler does. See
// test_atomic_var_init_1190.c for the C11/C17 side of the gate (a working
// expansion there).
#include <stdatomic.h>

int main(void) {
    atomic_int x = ATOMIC_VAR_INIT(5);
    return atomic_load(&x) == 5 ? 42 : 1;
}
