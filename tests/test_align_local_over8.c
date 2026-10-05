// Expected return: 42
// #1137: a local whose declared/natural alignment is > 8 must get an address
// aligned to it. bp's parity varies with the number of stack-passed args, so
// the callers below sweep it through every residue.

typedef float v4f32 __attribute__((vector_size(16)));

typedef struct {
    char c;
    _Alignas(32) int i;
} Over;

static int misaligned(void *p, int align) {
    return (unsigned long long)p % (unsigned long long)align != 0;
}

static int check_locals(int n) {
    char                   pad1                                    = (char)n;
    _Alignas(16) int       a16                                     = n;
    char                   pad2[3]                                 = {1, 2, 3};
    _Alignas(32) long long a32                                     = n + 1;
    int                    declarator __attribute__((aligned(16))) = n + 2;
    char                   pad3                                    = 7;
    _Alignas(64) char      a64    = (char)(n + 3);
    __int128               i128   = n + 4;
    v4f32                  v16    = {1, 2, 3, 4};
    Over                   o      = {1, n + 5};
    Over                   arr[2] = {{1, 10}, {2, 20}};
    long double            ld     = 1.5L;

    if (misaligned(&a16, 16))
        return 1;
    if (misaligned(&a32, 32))
        return 2;
    if (misaligned(&declarator, 16))
        return 3;
    if (misaligned(&a64, 64))
        return 4;
    if (misaligned(&i128, 16))
        return 5;
    if (misaligned(&v16, _Alignof(v4f32)))
        return 6;
    if (misaligned(&o, 32) || misaligned(&o.i, 32))
        return 9;
    if (misaligned(&arr[0], 32) || misaligned(&arr[1], 32))
        return 10;
    if (misaligned(&ld, _Alignof(long double)))
        return 11;

    if (a16 != n || a32 != n + 1 || declarator != n + 2 ||
        a64 != (char)(n + 3) || i128 != n + 4 || v16[3] != 4 || o.i != n + 5 ||
        arr[1].i != 20 || ld != 1.5L)
        return 12;
    if (pad1 != (char)n || pad2[2] != 3 || pad3 != 7)
        return 13;
    return 0;
}

static int pass0(int n) {
    return check_locals(n);
}
static int pass9(int a, int b, int c, int d, int e, int f, int g, int h, int i,
                 int n) {
    return check_locals(n + (a + b + c + d + e + f + g + h + i) * 0);
}
static int pass10(int a, int b, int c, int d, int e, int f, int g, int h, int i,
                  int j, int n) {
    return check_locals(n + (a + b + c + d + e + f + g + h + i + j) * 0);
}
static int pass11(int a, int b, int c, int d, int e, int f, int g, int h, int i,
                  int j, int k, int n) {
    return check_locals(n + (a + b + c + d + e + f + g + h + i + j + k) * 0);
}
static int pass12(int a, int b, int c, int d, int e, int f, int g, int h, int i,
                  int j, int k, int l, int n) {
    return check_locals(n +
                        (a + b + c + d + e + f + g + h + i + j + k + l) * 0);
}

static int deep(int depth, int n) {
    char pad[5] = {1, 2, 3, 4, 5};
    int  r      = depth ? deep(depth - 1, n + pad[depth % 5]) : pass0(n);
    return r;
}

int main(void) {
    int r;
    if ((r = pass0(1)))
        return 100 + r;
    if ((r = pass9(0, 0, 0, 0, 0, 0, 0, 0, 0, 2)))
        return 120 + r;
    if ((r = pass10(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3)))
        return 140 + r;
    if ((r = pass11(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4)))
        return 160 + r;
    if ((r = pass12(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5)))
        return 180 + r;
    for (int d = 0; d < 9; d++)
        if ((r = deep(d, d)))
            return 200 + r;
    return 42;
}
