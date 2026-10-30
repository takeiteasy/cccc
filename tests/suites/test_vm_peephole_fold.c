// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: exercises the VM's wrap-around of signed overflow
// Operations on constants held in registers are folded by the bytecode
// clean-up. Each result must equal the same operation on values the optimiser
// cannot see. Only a local used in a loop lives in a register, so every
// operation sits in a one-iteration loop over at most four scalars.

#include <limits.h>

typedef unsigned long long ull;

// The result goes through an immediate add, which folds once its operand is a
// known value.
#define T(e) ((long long)((ull)(e) + 1ULL))

long long opaque(long long v) {
    volatile long long t = v;
    return t;
}

#define CAT_(a, b) a##b
#define CAT(a, b)  CAT_(a, b)

// A function promotes only four scalars, so every pair of constants gets its own
// function.
#define A_LIST(X, M)                                                           \
    X(M, a0, 0) X(M, a1, 1) X(M, a2, -1) X(M, a3, 7) X(M, a4, LLONG_MAX)       \
    X(M, a5, LLONG_MIN)
#define B_LIST(X, M, IA, A)                                                    \
    X(M, IA, A, b0, 0) X(M, IA, A, b1, 1) X(M, IA, A, b2, -1)                  \
    X(M, IA, A, b3, 63) X(M, IA, A, b4, 64) X(M, IA, A, b5, LLONG_MIN)
#define C_LIST(X, M)                                                           \
    X(M, c0, 0) X(M, c1, 1) X(M, c2, -1) X(M, c3, 7) X(M, c4, 63)             \
    X(M, c5, 64) X(M, c6, 127) X(M, c7, 128) X(M, c8, 255) X(M, c9, 256)     \
    X(M, c10, 32767) X(M, c11, 32768) X(M, c12, 65535)                         \
    X(M, c13, 0x7fffffffLL) X(M, c14, 0x80000000LL)                            \
    X(M, c15, 0xffffffffLL) X(M, c16, LLONG_MAX) X(M, c17, LLONG_MIN)

#define NAME2(M, IA, IB) CAT(M, CAT(_, CAT(IA, IB)))
#define DEF2(M, IA, A, IB, B)                                                  \
    static void NAME2(M, IA, IB)(void) {                                       \
        M(A, B)                                                                \
    }
#define DEF2_ROW(M, IA, A) B_LIST(DEF2, M, IA, A)
#define CALL2(M, IA, A, IB, B) NAME2(M, IA, IB)();
#define CALL2_ROW(M, IA, A)    B_LIST(CALL2, M, IA, A)
#define DEFINE_GRID2(M)        A_LIST(DEF2_ROW, M)
#define RUN_GRID2(M)           A_LIST(CALL2_ROW, M)

#define DEF1(M, IC, C)                                                         \
    static void CAT(M, CAT(_, IC))(void) {                                     \
        M(C, 0)                                                                \
    }
#define CALL1(M, IC, C)  CAT(M, CAT(_, IC))();
#define DEFINE_GRID1(M)  C_LIST(DEF1, M)
#define RUN_GRID1(M)     C_LIST(CALL1, M)

#define REF_SETUP(A, B)                                                        \
    long long ox = opaque(A), oy = opaque(B);                                  \
    ull       uox = (ull)ox, uoy = (ull)oy;

#define ARITH(A, B)                                                            \
    do {                                                                       \
        long long x = (A), y = (B), g[6];                                      \
        for (int k = 0; k < 1; k++) {                                          \
            g[0] = T((long long)((ull)x + (ull)y));                               \
            g[1] = T((long long)((ull)x - (ull)y));                               \
            g[2] = T((long long)((ull)x * (ull)y));                               \
            g[3] = T(x & y);                                                      \
            g[4] = T(x | y);                                                      \
            g[5] = T(x ^ y);                                                      \
        }                                                                      \
        REF_SETUP(A, B)                                                        \
        AssertEq(g[0], T((long long)(uox + uoy)));                                \
        AssertEq(g[1], T((long long)(uox - uoy)));                                \
        AssertEq(g[2], T((long long)(uox * uoy)));                                \
        AssertEq(g[3], T(ox & oy));                                               \
        AssertEq(g[4], T(ox | oy));                                               \
        AssertEq(g[5], T(ox ^ oy));                                               \
    } while (0);

#define SHIFTS(A, B)                                                           \
    do {                                                                       \
        long long g[3];                                                        \
        g[0] = g[1] = g[2] = 0;                                                \
        long long x = (A), y = (B);                                            \
        for (int k = 0; k < 1; k++) {                                          \
            if (y >= 0 && y < 64) {                                            \
                g[0] = T((long long)((ull)x << y));                               \
                g[1] = T(x >> y);                                                 \
                g[2] = T((long long)((ull)x >> y));                               \
            }                                                                  \
        }                                                                      \
        REF_SETUP(A, B)                                                        \
        if (oy >= 0 && oy < 64) {                                              \
            AssertEq(g[0], T((long long)(uox << oy)));                            \
            AssertEq(g[1], T(ox >> oy));                                          \
            AssertEq(g[2], T((long long)(uox >> oy)));                            \
        }                                                                      \
    } while (0);

#define COMPARES(A, B)                                                         \
    do {                                                                       \
        long long x = (A), y = (B), g[10];                                     \
        for (int k = 0; k < 1; k++) {                                          \
            g[0] = T(x < y);                                                      \
            g[1] = T(x <= y);                                                     \
            g[2] = T(x > y);                                                      \
            g[3] = T(x >= y);                                                     \
            g[4] = T(x == y);                                                     \
            g[5] = T(x != y);                                                     \
            g[6] = T((ull)x < (ull)y);                                            \
            g[7] = T((ull)x <= (ull)y);                                           \
            g[8] = T((ull)x > (ull)y);                                            \
            g[9] = T((ull)x >= (ull)y);                                           \
        }                                                                      \
        REF_SETUP(A, B)                                                        \
        AssertEq(g[0], T(ox < oy));                                               \
        AssertEq(g[1], T(ox <= oy));                                              \
        AssertEq(g[2], T(ox > oy));                                               \
        AssertEq(g[3], T(ox >= oy));                                              \
        AssertEq(g[4], T(ox == oy));                                              \
        AssertEq(g[5], T(ox != oy));                                              \
        AssertEq(g[6], T(uox < uoy));                                             \
        AssertEq(g[7], T(uox <= uoy));                                            \
        AssertEq(g[8], T(uox > uoy));                                             \
        AssertEq(g[9], T(uox >= uoy));                                            \
    } while (0);

#define IMMEDIATES(A, B)                                                       \
    do {                                                                       \
        long long x = (A), g[15];                                              \
        for (int k = 0; k < 1; k++) {                                          \
            g[0]  = T((long long)((ull)x + 12345));                               \
            g[1]  = T((long long)((ull)x * 3));                                   \
            g[2]  = T((long long)((ull)x * (ull)-7));                             \
            g[3]  = T(x < 5);                                                     \
            g[4]  = T(x <= -1);                                                   \
            g[5]  = T(x > 0);                                                     \
            g[6]  = T(x >= 63);                                                   \
            g[7]  = T(x == 7);                                                    \
            g[8]  = T(x != 0);                                                    \
            g[9]  = T((ull)x < 5ULL);                                             \
            g[10] = T((ull)x <= 7ULL);                                            \
            g[11] = T((ull)x > 63ULL);                                            \
            g[12] = T((ull)x >= 0x80000000ULL);                                   \
            g[13] = T(5 < x);                                                     \
            g[14] = T(-1 >= x);                                                   \
        }                                                                      \
        REF_SETUP(A, B)                                                        \
        AssertEq(g[0], T((long long)(uox + 12345)));                              \
        AssertEq(g[1], T((long long)(uox * 3)));                                  \
        AssertEq(g[2], T((long long)(uox * (ull)-7)));                            \
        AssertEq(g[3], T(ox < 5));                                                \
        AssertEq(g[4], T(ox <= -1));                                              \
        AssertEq(g[5], T(ox > 0));                                                \
        AssertEq(g[6], T(ox >= 63));                                              \
        AssertEq(g[7], T(ox == 7));                                               \
        AssertEq(g[8], T(ox != 0));                                               \
        AssertEq(g[9], T(uox < 5ULL));                                            \
        AssertEq(g[10], T(uox <= 7ULL));                                          \
        AssertEq(g[11], T(uox > 63ULL));                                          \
        AssertEq(g[12], T(uox >= 0x80000000ULL));                                 \
        AssertEq(g[13], T(5 < ox));                                               \
        AssertEq(g[14], T(-1 >= ox));                                             \
    } while (0);

#define UNARY(A, B)                                                            \
    do {                                                                       \
        long long x = (A), g[10];                                               \
        for (int k = 0; k < 1; k++) {                                          \
            g[0] = T((long long)(0ULL - (ull)x));                                 \
            g[1] = T(~x);                                                         \
            g[2] = T(!x);                                                         \
            g[3] = T((signed char)x);                                             \
            g[4] = T((short)x);                                                   \
            g[5] = T((int)x);                                                     \
            g[6] = T((unsigned char)x);                                           \
            g[7] = T((unsigned short)x);                                          \
            g[8] = T((unsigned)x);                                                \
            g[9] = T(-x);                                                         \
        }                                                                      \
        REF_SETUP(A, B)                                                        \
        AssertEq(g[0], T((long long)(0ULL - uox)));                               \
        AssertEq(g[1], T(~ox));                                                   \
        AssertEq(g[2], T(!ox));                                                   \
        AssertEq(g[3], T((signed char)ox));                                       \
        AssertEq(g[4], T((short)ox));                                             \
        AssertEq(g[5], T((int)ox));                                               \
        AssertEq(g[6], T((unsigned char)ox));                                     \
        AssertEq(g[7], T((unsigned short)ox));                                    \
        AssertEq(g[8], T((unsigned)ox));                                          \
        AssertEq(g[9], T(-ox));                                                   \
    } while (0);

DEFINE_GRID2(ARITH)
DEFINE_GRID2(SHIFTS)
DEFINE_GRID2(COMPARES)
DEFINE_GRID1(IMMEDIATES)
DEFINE_GRID1(UNARY)

[[cccc::test]]
void test_fold_arithmetic(void) {
    RUN_GRID2(ARITH)
}

[[cccc::test]]
void test_fold_shifts(void) {
    RUN_GRID2(SHIFTS)
}

[[cccc::test]]
void test_fold_compares(void) {
    RUN_GRID2(COMPARES)
}

[[cccc::test]]
void test_fold_immediates(void) {
    RUN_GRID1(IMMEDIATES)
}

[[cccc::test]]
void test_fold_unary_and_extensions(void) {
    RUN_GRID1(UNARY)
}

static long long shift_top(void) {
    long long one = 1, top = 63, r = 0;
    for (int k = 0; k < 1; k++)
        r = (long long)((ull)one << top);
    return r;
}

static long long ushr_top(void) {
    long long m = -1, top = 63, r = 0;
    for (int k = 0; k < 1; k++)
        r = (long long)((ull)m >> top);
    return r;
}

static long long sar_top(void) {
    long long m = -1, top = 63, r = 0;
    for (int k = 0; k < 1; k++)
        r = m >> top;
    return r;
}

[[cccc::test]]
void test_fold_extreme_shift_amounts(void) {
    AssertEq(shift_top(), LLONG_MIN);
    AssertEq(ushr_top(), 1LL);
    AssertEq(sar_top(), -1LL);
}

// An identity operand turns the operation into a copy.
#define IDENT(NAME, CONST, EXPR)                                               \
    static long long NAME(long long v) {                                       \
        long long c = CONST, r = 0;                                            \
        for (int k = 0; k < 1; k++)                                            \
            r = EXPR;                                                          \
        return r;                                                              \
    }

IDENT(add_zero, 0, v + c)
IDENT(zero_add, 0, c + v)
IDENT(sub_zero, 0, v - c)
IDENT(or_zero, 0, v | c)
IDENT(zero_or, 0, c | v)
IDENT(xor_zero, 0, v ^ c)
IDENT(zero_xor, 0, c ^ v)
IDENT(shl_zero, 0, (long long)((ull)v << c))
IDENT(sar_zero, 0, v >> c)
IDENT(shr_zero, 0, (long long)((ull)v >> c))
IDENT(mul_one, 1, v *c)
IDENT(one_mul, 1, c *v)
IDENT(and_ones, -1, v &c)
IDENT(ones_and, -1, c &v)
IDENT(mul_zero, 0, v *c)
IDENT(zero_mul, 0, c *v)
IDENT(and_zero, 0, v &c)
IDENT(zero_and, 0, c &v)

[[cccc::test]]
void test_fold_identity_operands(void) {
    long long v = opaque(123456789012LL);
    AssertEq(add_zero(v), v);
    AssertEq(zero_add(v), v);
    AssertEq(sub_zero(v), v);
    AssertEq(or_zero(v), v);
    AssertEq(zero_or(v), v);
    AssertEq(xor_zero(v), v);
    AssertEq(zero_xor(v), v);
    AssertEq(shl_zero(v), v);
    AssertEq(sar_zero(v), v);
    AssertEq(shr_zero(v), v);
    AssertEq(mul_one(v), v);
    AssertEq(one_mul(v), v);
    AssertEq(and_ones(v), v);
    AssertEq(ones_and(v), v);
    AssertEq(mul_zero(v), 0LL);
    AssertEq(zero_mul(v), 0LL);
    AssertEq(and_zero(v), 0LL);
    AssertEq(zero_and(v), 0LL);
}

// A constant index scales into an immediate add.
static int idx_load(int *p) {
    int k = 5, r = 0;
    for (int i = 0; i < 1; i++)
        r = *(p + k);
    return r;
}

static long idx_load_long(long *p) {
    int  k = 3;
    long r = 0;
    for (int i = 0; i < 1; i++)
        r = p[k];
    return r;
}

static int idx_advance(int *p) {
    int k = 5;
    for (int i = 0; i < 1; i++)
        p += k;
    return *p;
}

[[cccc::test]]
void test_fold_constant_index(void) {
    int  arr[8];
    long big[4];
    for (int i = 0; i < 8; i++)
        arr[i] = i * 10;
    for (int i = 0; i < 4; i++)
        big[i] = (long)i * 1000;
    AssertEq(idx_load(arr), 50);
    AssertEq(idx_load_long(big), 3000L);
    AssertEq(idx_advance(arr), 50);
}

// A zero stored or compared is read from the zero register.
static void zero_fill_char(char *b, int n) {
    for (int j = 0; j < n; j++)
        b[j] = 0;
}

static void zero_fill_short(short *h, int n) {
    for (int j = 0; j < n; j++)
        h[j] = 0;
}

static void zero_fill_int(int *w, int n) {
    for (int j = 0; j < n; j++)
        w[j] = 0;
}

static void zero_fill_long(long *d, int n) {
    for (int j = 0; j < n; j++)
        d[j] = 0;
}

static int zero_copy(int seed) {
    int z = 0, copy = seed, n = 0;
    for (int j = 0; j < 2; j++) {
        copy = z;
        n += copy;
    }
    return copy + n + 7;
}

[[cccc::test]]
void test_fold_zero_register(void) {
    char  b[4] = {1, 2, 3, 4};
    short h[4] = {1, 2, 3, 4};
    int   w[4] = {1, 2, 3, 4};
    long  d[4] = {1, 2, 3, 4};
    zero_fill_char(b, 4);
    zero_fill_short(h, 4);
    zero_fill_int(w, 4);
    zero_fill_long(d, 4);
    for (int j = 0; j < 4; j++) {
        AssertEq(b[j], 0);
        AssertEq(h[j], 0);
        AssertEq(w[j], 0);
        AssertEq(d[j], 0L);
    }
    AssertEq(zero_copy(99), 7);
}

// A value known on one side of a branch, or equal on both, folds after it.
static long long join_fold(long long sel) {
    long long n = 5, r = 0;
    for (int k = 0; k < 1; k++) {
        if (sel)
            n = n + 1;
        else
            n = n + 1;
        r = n * 3;
    }
    return r;
}

static long long eq_edge(long long v) {
    long long r = 0;
    for (int k = 0; k < 1; k++) {
        if (v == 17)
            r = v * 2;
        else
            r = v;
    }
    return r;
}

[[cccc::test]]
void test_fold_across_branches(void) {
    AssertEq(join_fold(opaque(0)), 18LL);
    AssertEq(join_fold(opaque(1)), 18LL);
    AssertEq(eq_edge(opaque(17)), 34LL);
    AssertEq(eq_edge(opaque(3)), 3LL);
}
