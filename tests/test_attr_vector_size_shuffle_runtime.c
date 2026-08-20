// GNU __builtin_shuffle with a RUNTIME/named vector index mask (tracker
// #723, follow-up to #715's compile-time-constant-only form). The mask is
// an ordinary integer vector value -- named local, function parameter, or
// computed at runtime -- not a bare brace list. No new opcode: lowered via
// the same vector-subscript lvalue machinery as the constant form, with a
// runtime index instead of a literal one. Out-of-range indices WRAP modulo
// the lane count (1-vector form) or 2x the lane count (2-vector form),
// matching GCC's documented __builtin_shuffle semantics -- this differs
// from the constant-mask form, which rejects an out-of-range index at
// compile time (see test_attr_vector_size_shuffle.c).

typedef int   v4si __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));

// Mask passed as a function parameter.
static v4si shuffle_it(v4si v, v4si mask) {
    return __builtin_shuffle(v, mask);
}

int main(void) {
    v4si a = {10, 20, 30, 40};
    v4si b = {50, 60, 70, 80};

    // 1-vector form: named runtime mask, reverse.
    v4si m1 = {3, 2, 1, 0};
    v4si r1 = __builtin_shuffle(a, m1);
    if (r1[0] != 40)
        return 1;
    if (r1[1] != 30)
        return 2;
    if (r1[2] != 20)
        return 3;
    if (r1[3] != 10)
        return 4;

    // 2-vector form: named runtime mask, interleave.
    v4si m2 = {0, 4, 1, 5};
    v4si r2 = __builtin_shuffle(a, b, m2);
    if (r2[0] != 10)
        return 5;
    if (r2[1] != 50)
        return 6;
    if (r2[2] != 20)
        return 7;
    if (r2[3] != 60)
        return 8;

    // Mask passed as a function parameter.
    v4si r3 = shuffle_it(a, m1);
    if (r3[0] != 40 || r3[3] != 10)
        return 9;

    // Mask computed at runtime (not a compile-time constant).
    int  cond    = 1;
    v4si dynmask = {0, 0, 0, 0};
    dynmask[0]   = cond ? 2 : 0;
    v4si r4      = __builtin_shuffle(a, dynmask);
    if (r4[0] != 30)
        return 10;

    // Float lanes with an int mask.
    v4sf fa = {1.5f, 2.5f, 3.5f, 4.5f};
    v4si fm = {3, 2, 1, 0};
    v4sf fr = __builtin_shuffle(fa, fm);
    if (fr[0] != 4.5f)
        return 11;
    if (fr[3] != 1.5f)
        return 12;

    // Out-of-range 1-vector index wraps: index 7 on a 4-lane vector wraps
    // to lane 3 (7 % 4 == 3).
    v4si m5 = {7, 0, 0, 0};
    v4si r5 = __builtin_shuffle(a, m5);
    if (r5[0] != 40)
        return 13;

    // Out-of-range 2-vector index wraps: index 9 wraps to 1 (9 % 8 == 1),
    // selecting a[1] == 20.
    v4si m6 = {9, 0, 0, 0};
    v4si r6 = __builtin_shuffle(a, b, m6);
    if (r6[0] != 20)
        return 14;

    return 42;
}
