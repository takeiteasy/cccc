// Expected return: 42
// #1137: by-value over-aligned params, argument scratch temps, nested-function
// access and block captures must all see correctly aligned addresses.

typedef float v4f32 __attribute__((vector_size(16)));
typedef struct {
    char c;
    _Alignas(32) int i;
} Over;

static int misaligned(void *p, int align) {
    return (unsigned long long)p % (unsigned long long)align != 0;
}

static int take_vec(int pad, v4f32 v) {
    if (misaligned(&v, _Alignof(v4f32)))
        return 1;
    return v[3] == 4 ? 0 : 3;
}

static int take_over(char a, Over o, __int128 x, long double ld) {
    if (misaligned(&o, 32) || misaligned(&o.i, 32))
        return 1;
    if (misaligned(&x, 16))
        return 2;
    if (misaligned(&ld, _Alignof(long double)))
        return 3;
    return o.i == 9 && x == 5 && ld == 2.5L ? 0 : 4;
}

static int with_stack_args(int a, int b, int c, int d, int e, int f, int g,
                           int h, int i, long double ld, __int128 x) {
    if (misaligned(&ld, _Alignof(long double)))
        return 1;
    if (misaligned(&x, 16))
        return 2;
    return ld == 3.5L && x == 6 && a + b + c + d + e + f + g + h + i == 45 ? 0
                                                                           : 3;
}

static int nested(int n) {
    _Alignas(32) int       a32 = n;
    _Alignas(16) long long a16 = n + 1;
    int inner(void) {
        if (misaligned(&a32, 32) || misaligned(&a16, 16))
            return 1;
        a32 += 1;
        a16 += 1;
        return 0;
    }
    int r = inner();
    if (r)
        return r;
    return a32 == n + 1 && a16 == n + 2 ? 0 : 2;
}

int main(void) {
    v4f32 v = {1, 2, 3, 4};
    int   r;
    if ((r = take_vec(1, v)))
        return 10 + r;
    Over o = {1, 9};
    if ((r = take_over(1, o, 5, 2.5L)))
        return 20 + r;
    if ((r = with_stack_args(1, 2, 3, 4, 5, 6, 7, 8, 9, 3.5L, 6)))
        return 30 + r;
    for (int n = 0; n < 4; n++)
        if ((r = nested(n)))
            return 40 + r;
    return 42;
}
