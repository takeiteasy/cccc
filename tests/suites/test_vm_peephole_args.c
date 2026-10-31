// CCCC_FLAGS: --testing
// A call keeps the argument registers it was emitted with live and no others.
// Each callee here checks every argument, so a definition the caller's
// peephole dropped by mistake shows up as a wrong value.

#include <stdio.h>
#include <string.h>

typedef struct {
    long a, b, c;
} Triple;

typedef int V4 __attribute__((vector_size(16)));

long take8(long a, long b, long c, long d, long e, long f, long g, long h) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h;
}
long take10(long a, long b, long c, long d, long e, long f, long g, long h,
            long i, long j) {
    return take8(a, b, c, d, e, f, g, h) + 9 * i + 10 * j;
}
long take1(long a) {
    return a;
}
long take0(void) {
    return 7;
}
double mix(int a, double b, int c, float d, long e) {
    return a + b * 2 + c * 3 + d * 4 + e * 5;
}
long wide_then(__int128 w, long after) {
    return (long)(w >> 64) * 100 + (long)w * 10 + after;
}
long ld_then(long double x, long after) {
    return (long)x * 10 + after;
}
long struct_then(Triple t, long after) {
    return t.a * 1000 + t.b * 100 + t.c * 10 + after;
}
long vec_then(V4 v, long after) {
    return v[0] * 1000 + v[3] * 10 + after;
}
long bitint_then(_BitInt(200) w, long after) {
    return (long)(w >> 100) * 10 + after;
}
// Seven live values in registers around a one-argument call.
long around_call(long x) {
    long s0 = x + 1, s1 = x + 2, s2 = x + 3;
    long r = take1(s0 + s1 + s2);
    return r + s0 + s1 + s2;
}

long via_pointer(long (*fn)(long, long, long), long x) {
    return fn(x, x + 1, x + 2);
}
long sum3(long a, long b, long c) {
    return a * 100 + b * 10 + c;
}

long nested_calls(long x) {
    long inner(long y) {
        return x * 10 + y;
    }
    return inner(1) + inner(2);
}

[[cccc::test(return = 42)]]
int test_direct_calls(void) {
    if (take0() != 7)
        return 1;
    if (take1(5) != 5)
        return 2;
    if (take8(1, 2, 3, 4, 5, 6, 7, 8) != 204)
        return 3;
    if (take10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 204 + 81 + 100)
        return 4;
    if (around_call(10) != 36 + 11 + 12 + 13)
        return 5;
    return 42;
}

[[cccc::test(return = 42)]]
int test_float_and_mixed_arguments(void) {
    if (mix(1, 2.0, 3, 4.0f, 5) != 1 + 4 + 9 + 16 + 25)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_multi_register_arguments_then_another(void) {
    if (wide_then(((__int128)3 << 64) | 4, 5) != 345)
        return 1;
    if (ld_then(6.0L, 7) != 67)
        return 2;
    if (struct_then((Triple){1, 2, 3}, 4) != 1234)
        return 4;
    if (vec_then((V4){1, 2, 3, 4}, 5) != 1045)
        return 5;
    if (bitint_then(((_BitInt(200))9 << 100) | 1, 2) != 92)
        return 6;
    return 42;
}

[[cccc::test(return = 42)]]
int test_indirect_calls(void) {
    if (via_pointer(sum3, 1) != 123)
        return 2;
    long (*one)(long) = take1;
    if (one(9) != 9)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_nested_function_static_link(void) {
    if (nested_calls(3) != 31 + 32)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_native_calls(void) {
    char buf[64];
    int  n = snprintf(buf, sizeof buf, "%d-%s-%.1f-%ld", 4, "x", 2.5, 9L);
    if (n != 9 || strcmp(buf, "4-x-2.5-9") != 0)
        return 1;
    if (strlen("abc") != 3)
        return 2;
    if (memcmp("ab", "ac", 1) != 0)
        return 3;
    return 42;
}
