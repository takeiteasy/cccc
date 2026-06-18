/*
 * Regression test: __block aggregate partial-init must zero unspecified elements (#473)
 *
 * ND_MEMZERO codegen (#464) skipped __block variables to avoid corrupting the
 * 8-byte heap pointer in their stack slot.  Side-effect: for `__block T x[N] =
 * {partial}`, the unspecified elements were left uninitialised (the MALC'd heap
 * cell was never pre-zeroed).
 *
 * Fix: dereference the stack slot to get the heap pointer, then MSET through it.
 *
 * Strategy: use --memory-poisoning so MALC fills heap memory with 0xCD.  Without
 * the fix, unspecified elements will read 0xCD instead of 0.
 */
// CCCC_FLAGS: -O0 --memory-poisoning
#include <stdio.h>

struct S {
    int  a;
    char b;
    long c;
    char d[8];
};

/* Test 1: __block array -- only first element explicit, rest must be 0. */
static int test_block_array(void) {
    __block int x[4] = {1};
    if (x[0] != 1) return 1; /* explicit element must be set */
    if (x[1] != 0) return 2; /* unspecified must be zeroed */
    if (x[2] != 0) return 3;
    if (x[3] != 0) return 4;
    return 0;
}

/* Test 2: __block struct -- only .a explicit, all other members must be 0. */
static int test_block_struct(void) {
    __block struct S s = {.a = 42};
    if (s.a != 42) return 1;
    if (s.b != 0)  return 2;
    if (s.c != 0)  return 3;
    for (int i = 0; i < 8; i++)
        if (s.d[i] != 0) return 4;
    return 0;
}

/* Test 3: block captures and reads the __block var after partial init, proving
   the heap storage (not just the local analysis) is correctly zeroed. */
static int test_block_capture_reads_partial_init(void) {
    __block int arr[4] = {99};
    __block int result = 0;
    void (^check)(void) = ^{
        if (arr[0] != 99) result = 1;
        if (arr[1] != 0)  result = 2;
        if (arr[2] != 0)  result = 3;
        if (arr[3] != 0)  result = 4;
    };
    check();
    return result;
}

int main(void) {
    int r;

    r = test_block_array();
    if (r) { printf("block array fail at case %d\n", r); return 1; }

    r = test_block_struct();
    if (r) { printf("block struct fail at case %d\n", r); return 2; }

    r = test_block_capture_reads_partial_init();
    if (r) { printf("block capture partial-init fail at case %d\n", r); return 3; }

    return 42;
}
