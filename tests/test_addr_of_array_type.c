// Test: `&a` on a fixed-size array has the standard (non-decayed) type
// `T (*)[N]`, not `T *`.
// Expected return: 42
//
// #975: add_type()'s ND_ADDR case used to special-case TY_ARRAY, decaying
// `&a` to `pointer_to(ty->base)` (element-pointer-typed, chibicc legacy) --
// non-standard: real gcc/clang give `&a` type `int (*)[N]`, so `&a + 1`
// strides the whole array, not one element. This mirrors the TY_VLA shape
// #973 already established for `&v` on a variable-length array. Only the
// static *type* changed here; array-to-pointer decay (a bare array name
// used as a value) is untouched -- see the ND_VAR case in codegen.c, not
// this ND_ADDR case.

int main(void) {
    int a[3];
    a[0] = 1;
    a[1] = 2;
    a[2] = 3;

    // &a + 1 must stride the whole array (12 bytes for int[3]), not one
    // element (4 bytes) -- the exact bug measured in the ticket.
    long stride = (long)((char *)(&a + 1) - (char *)&a);
    if (stride != (long)sizeof(a))
        return 1;

    // sizeof(*&a) must be the whole array's size, confirming &a's pointee
    // type is the array itself, not its element type.
    if (sizeof(*&a) != sizeof(a))
        return 2;

    // (void*)&a and (void*)a (the array's own decay-to-pointer value) must
    // still be the same address -- only the static type changed, not the
    // value produced.
    if ((void *)&a != (void *)a)
        return 3;

    // &a[0] - a (both element-pointer-typed) is unaffected: still ordinary
    // element-count pointer subtraction.
    if (&a[0] - a != 0)
        return 4;

    // &a - &a is unaffected: still zero, whatever the pointee type.
    if (&a - &a != 0)
        return 5;

    // A pointer-to-array round trip through &a still reads/writes the same
    // storage as the array itself.
    int (*p)[3] = &a;
    (*p)[1] += 10;
    if (a[1] != 12)
        return 6;

    return 42;
}
