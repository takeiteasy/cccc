// vector_size accepts any power-of-two width up to 64 bytes: 8-, 4- and
// 2-byte vectors behave like the wider ones, including the bitwise operators.
#include <stdarg.h>

typedef float         f2 __attribute__((vector_size(8)));
typedef int           i2 __attribute__((vector_size(8)));
typedef char          c4 __attribute__((vector_size(4)));
typedef short         s2 __attribute__((vector_size(4)));
typedef unsigned char c2 __attribute__((vector_size(2)));

struct S {
    char pad;
    c4   v;
    f2   w;
};

static f2 add(f2 a, f2 b) {
    return a + b;
}

static float second(int n, ...) {
    va_list ap;
    va_start(ap, n);
    f2 v = va_arg(ap, f2);
    va_end(ap);
    return v[1];
}

int main(void) {
    if (sizeof(f2) != 8 || sizeof(c4) != 4 || sizeof(s2) != 4 ||
        sizeof(c2) != 2)
        return 1;
    if (_Alignof(f2) != 8 || _Alignof(c4) != 4 || _Alignof(c2) != 2)
        return 2;

    f2 a = {1, 2}, b = {3, 4};
    f2 c = add(a, b) * b;
    if (c[0] != 12 || c[1] != 24)
        return 3;

    c4 x = {1, 2, 3, 4}, y = {7, 7, 7, 7};
    c4 z = (x & y) | (x ^ y);
    for (int i = 0; i < 4; i++)
        if (z[i] != 7)
            return 4;
    c4 n = ~x;
    if (n[0] != -2 || n[3] != -5)
        return 5;

    s2 t = {5, 6};
    s2 u = t + t;
    if (u[0] != 10 || u[1] != 12)
        return 6;

    c2 q = {200, 3};
    c2 r = q + q;
    if (r[0] != 144 || r[1] != 6)
        return 7;

    i2 m = a < b;
    if (m[0] != -1 || m[1] != -1)
        return 8;

    struct S st = {1, x, c};
    if (sizeof(st) != 16 || st.w[1] != 24 || st.v[3] != 4)
        return 9;

    if (second(0, c) != 24)
        return 10;
    return 42;
}
