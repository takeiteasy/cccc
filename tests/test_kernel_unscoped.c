// EXPECT_COMPILE_ERROR
// Unscoped [[kernel]] is the cccc attribute: the kernel-code check applies.
// CCCC_EXPECT_STDERR: calling 'free', which has no definition in this file, is not allowed in kernel code

#include <stdlib.h>

[[kernel]] static void k(void) {
    free(NULL);
}

int main(void) {
    k();
    return 42;
}
