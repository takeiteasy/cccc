/*
 * Regression test: partial aggregate initialiser zero-fill (#464 / ND_MEMZERO)
 *
 * ND_MEMZERO was a stub no-op in codegen, so `= {0}` did NOT zero unspecified
 * array/struct elements -- they kept whatever stack garbage was present.  MSET
 * now implements it correctly.
 *
 * Strategy: dirty() writes 0xAB into its entire stack frame, then returns.
 * When test_array() / test_struct() are called next they reuse the same stack
 * region; any element NOT explicitly initialised would still be 0xAB.
 *
 * Pin to -O0 to prevent the optimiser from constant-folding away the reads.
 */
// CCCC_FLAGS: -O0
#include <stdio.h>

/* Fill the stack frame with 0xAB so the caller's frame is dirty. */
static void dirty(void) {
    volatile unsigned char buf[256];
    for (int i = 0; i < 256; i++) buf[i] = 0xAB;
    (void)buf[0]; /* prevent elision */
}

static int test_array(void) {
    unsigned char seen[256] = {0};
    for (int i = 0; i < 256; i++)
        if (seen[i]) return i + 1; /* non-zero byte → fail */
    return 0;
}

struct S { int a; char b; long c; char d[8]; };

static int test_struct(void) {
    struct S s = {.a = 1}; /* only .a explicit; .b/.c/.d must be zeroed */
    if (s.b != 0) return 1;
    if (s.c != 0) return 2;
    for (int i = 0; i < 8; i++)
        if (s.d[i] != 0) return 3;
    return 0;
}

int main(void) {
    dirty();
    int r = test_array();
    if (r) { printf("array fail at %d\n", r); return 1; }

    dirty();
    r = test_struct();
    if (r) { printf("struct fail at %d\n", r); return 2; }

    return 42;
}
