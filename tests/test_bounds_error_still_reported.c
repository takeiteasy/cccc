// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2
// #982 positive control for CHKB (the ADD form): the CHKB/CHKBN split and
// the pointer-difference exclusion in codegen.c must not silently disable
// bounds checking wholesale. A genuine out-of-bounds forward access must
// still trap. Offset and element type are pinned so the access is
// unambiguously past the end for ANY element size, not just `char`.
#include <stdlib.h>
int main(void) {
    int *p = malloc(4 * sizeof(int)); // valid indices 0..3
    if (!p)
        return 255;
    p[8] = 1; // 8 elements past a 4-element allocation -- always OOB
    free(p);
    return 42;
}
