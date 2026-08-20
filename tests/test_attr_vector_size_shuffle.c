// GNU __builtin_shuffle (tracker #715), restricted to a compile-time-constant
// brace-enclosed index mask (CCCC's constant-mask form -- see COVERAGE.md for
// how this differs from upstream GCC's general vector-typed mask argument).
// Covers the 1-vector (permute) and 2-vector (blend) forms.

typedef int   v4si __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));

int main(void) {
    v4si a = {1, 2, 3, 4};

    // 1-vector form: reverse.
    v4si rev = __builtin_shuffle(a, {3, 2, 1, 0});
    if (rev[0] != 4)
        return 1;
    if (rev[1] != 3)
        return 2;
    if (rev[2] != 2)
        return 3;
    if (rev[3] != 1)
        return 4;

    // 1-vector form: broadcast lane 0.
    v4si bcast = __builtin_shuffle(a, {0, 0, 0, 0});
    if (bcast[0] != 1 || bcast[1] != 1 || bcast[2] != 1 || bcast[3] != 1)
        return 5;

    // 2-vector form: interleave.
    v4si b           = {10, 20, 30, 40};
    v4si interleaved = __builtin_shuffle(a, b, {0, 4, 1, 5});
    if (interleaved[0] != 1)
        return 6;
    if (interleaved[1] != 10)
        return 7;
    if (interleaved[2] != 2)
        return 8;
    if (interleaved[3] != 20)
        return 9;

    // 2-vector form: take-all-from-second.
    v4si allb = __builtin_shuffle(a, b, {4, 5, 6, 7});
    if (allb[0] != 10 || allb[1] != 20 || allb[2] != 30 || allb[3] != 40)
        return 10;

    // Float lanes.
    v4sf fa   = {1.5f, 2.5f, 3.5f, 4.5f};
    v4sf frev = __builtin_shuffle(fa, {3, 2, 1, 0});
    if (frev[0] != 4.5f)
        return 11;
    if (frev[3] != 1.5f)
        return 12;

    return 42;
}
