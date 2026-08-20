// GNU __builtin_convertvector (tracker #715): cross-lane-family element
// conversion between vectors with the same lane count. Restricted to
// int32<->float32 and int64<->float64 pairs (the only pairs representable
// within the 16-byte VM vector register -- see COVERAGE.md). Integer
// conversion truncates toward zero (C cast semantics, matches GCC).

typedef int    v4si __attribute__((vector_size(16)));
typedef float  v4sf __attribute__((vector_size(16)));
typedef long   v2di __attribute__((vector_size(16)));
typedef double v2df __attribute__((vector_size(16)));

int main(void) {
    v4sf f = {1.9f, -1.9f, 2.1f, -2.1f};
    v4si i = __builtin_convertvector(f, v4si);
    if (i[0] != 1)
        return 1; // truncates toward zero, not round
    if (i[1] != -1)
        return 2;
    if (i[2] != 2)
        return 3;
    if (i[3] != -2)
        return 4;

    v4si i2 = {1, -1, 100, -100};
    v4sf f2 = __builtin_convertvector(i2, v4sf);
    if (f2[0] != 1.0f)
        return 5;
    if (f2[1] != -1.0f)
        return 6;
    if (f2[2] != 100.0f)
        return 7;
    if (f2[3] != -100.0f)
        return 8;

    v2df d = {3.9, -3.9};
    v2di l = __builtin_convertvector(d, v2di);
    if (l[0] != 3)
        return 9;
    if (l[1] != -3)
        return 10;

    v2di l2 = {7, -7};
    v2df d2 = __builtin_convertvector(l2, v2df);
    if (d2[0] != 7.0)
        return 11;
    if (d2[1] != -7.0)
        return 12;

    return 42;
}
