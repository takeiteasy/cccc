#include <limits.h>
#include <stdlib.h>

// A 32-bit result wrapped by the VM must still read as the value its type
// denotes wherever more than its low 32 bits are read: a wider compare, a
// right shift, a division, a conversion, a call argument and a return.
// Values come through noinline parameters so nothing is folded.

typedef long long ll;

[[gnu::noinline]] ll widen(int a, int b) {
    int s = a + b;
    return s;
}

[[gnu::noinline]] ll widen_u(unsigned u) {
    return (ll)(u - 3);
}

[[gnu::noinline]] unsigned shr_u(unsigned u) {
    return (u - 3) >> 1;
}

[[gnu::noinline]] double to_double(unsigned u) {
    return (double)(u - 3);
}

[[gnu::noinline]] int shr_s(int a, int b) {
    return (a * b) >> 3;
}

[[gnu::noinline]] int divide(int a, int b, int c) {
    return (a + b) / c;
}

[[gnu::noinline]] int cmp_wide(int a, int b) {
    ll  wide   = (ll)a * b;
    int narrow = a * b;
    return wide == narrow;
}

[[gnu::noinline]] int less(int a, int b) {
    return a + b < 0;
}

[[gnu::noinline]] int passes(int a, int b) {
    return widen(a + b, 0) == (ll)(int)((unsigned)a + (unsigned)b);
}

[[gnu::noinline]] int chain(int a, int b, int c, int d) {
    return (a * b + c) ^ d;
}

[[gnu::noinline]] void store_int(int *p, int a, int b) {
    *p = a * b + 1;
}

[[gnu::noinline]] void store_short(short *p, int a, int b) {
    *p = (short)(a + b);
}

[[gnu::noinline]] void store_char(signed char *p, int a, int b) {
    *p = (signed char)(a - b);
}

[[gnu::noinline]] ll shift_count(int n) {
    return (ll)(1 << n) >> 1;
}

[[gnu::noinline]] ll index_with(ll *tab, int i, int j) {
    return tab[i + j];
}

[[gnu::noinline]] int accumulate(int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += i * 3 ^ (i >> 2);
    return s;
}

[[gnu::noinline]] unsigned accumulate_u(unsigned n) {
    unsigned s = 0;
    for (unsigned i = 0; i < n; i++)
        s = s * 31u + (i ^ 0x9e3779b9u);
    return s;
}

[[gnu::noinline]] ll mixed(int a, unsigned b) {
    return a + b;
}

int main(void) {
    if (widen(INT_MAX, 1) != INT_MIN)
        return 1;
    if (widen_u(2) != 4294967295LL)
        return 2;
    if (shr_u(2) != 0x7fffffffu)
        return 3;
    if (to_double(2) != 4294967295.0)
        return 4;
    if (shr_s(INT_MAX, 2) != (int)((unsigned)INT_MAX * 2u) >> 3)
        return 5;
    if (divide(INT_MAX, 1, 3) != INT_MIN / 3)
        return 6;
    if (cmp_wide(65536, 65536) != 0 || cmp_wide(3, 5) != 1)
        return 7;
    if (!less(INT_MAX, 1) || less(1, 2))
        return 8;
    if (!passes(INT_MAX, INT_MAX))
        return 9;
    if (chain(65536, 65536, 7, 1) != 6)
        return 10;
    if (chain(INT_MAX, 2, 0, 0) != -2)
        return 11;

    int i32 = 0;
    store_int(&i32, 65536, 65537);
    if (i32 != (int)(65536u * 65537u + 1u))
        return 12;
    short s16 = 0;
    store_short(&s16, 30000, 30000);
    if (s16 != (short)60000)
        return 13;
    signed char s8 = 0;
    store_char(&s8, 0, 200);
    if (s8 != (signed char)-200)
        return 14;
    if (shift_count(31) != (ll)INT_MIN >> 1)
        return 15;

    ll tab[4] = {10, 20, 30, 40};
    if (index_with(tab + 2, INT_MAX, INT_MIN + 2) != 40)
        return 16;
    if (index_with(tab + 3, -1, -1) != 20)
        return 17;

    int ref = 0;
    for (int i = 0; i < 1000; i++)
        ref = (int)((unsigned)ref + (unsigned)((i * 3) ^ (i >> 2)));
    if (accumulate(1000) != ref)
        return 18;
    unsigned uref = 0;
    for (unsigned i = 0; i < 1000; i++)
        uref = uref * 31u + (i ^ 0x9e3779b9u);
    if (accumulate_u(1000) != uref)
        return 19;
    if (mixed(-1, 1) != 0 || mixed(-1, 2) != 1)
        return 20;
    return 42;
}
