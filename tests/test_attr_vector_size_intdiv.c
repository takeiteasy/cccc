// GNU vector_size integer lane division and modulo (tracker #715): / and %
// on integer-lane vectors, across all four integer lane widths. Per-lane
// zero-divisor and MIN/-1-overflow trapping is covered separately in
// test_attr_vector_size_intdiv_zero_error.c and
// test_attr_vector_size_intdiv_overflow_error.c.

typedef int   v4si __attribute__((vector_size(16)));
typedef long  v2di __attribute__((vector_size(16)));
typedef short v8hi __attribute__((vector_size(16)));
typedef char  v16qi __attribute__((vector_size(16)));

int main(void) {
    v4si a = {10, -10, 7, -7};
    v4si b = {3, 3, -3, -3};

    v4si d = a / b; // 3, -3, -2, 2 (truncating division)
    if (d[0] != 3)
        return 1;
    if (d[1] != -3)
        return 2;
    if (d[2] != -2)
        return 3;
    if (d[3] != 2)
        return 4;

    v4si m = a % b; // 1, -1, 1, -1
    if (m[0] != 1)
        return 5;
    if (m[1] != -1)
        return 6;
    if (m[2] != 1)
        return 7;
    if (m[3] != -1)
        return 8;

    // Scalar-broadcast divisor.
    v4si d2 = a / 5;
    if (d2[0] != 2)
        return 9;
    if (d2[1] != -2)
        return 10;

    v2di la = {100, -100};
    v2di lb = {7, 7};
    v2di ld = la / lb;
    if (ld[0] != 14)
        return 11;
    if (ld[1] != -14)
        return 12;
    v2di lm = la % lb;
    if (lm[0] != 2)
        return 13;
    if (lm[1] != -2)
        return 14;

    v8hi sa = {100, -100, 50, -50, 7, 7, 7, 7};
    v8hi sb = {9, 9, 6, 6, 2, 3, 4, 5};
    v8hi sd = sa / sb;
    if (sd[0] != 11)
        return 15;
    if (sd[1] != -11)
        return 16;
    if (sd[2] != 8)
        return 17;
    if (sd[3] != -8)
        return 18;

    v16qi ba = {100, -100, 50, -50, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    v16qi bb = {9, 9, 6, 6, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    v16qi bd = ba / bb;
    if (bd[0] != 11)
        return 19;
    if (bd[1] != -11)
        return 20;

    return 42;
}
