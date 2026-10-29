// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: exercises the VM's wrap-around of signed overflow
// The always-on bytecode clean-up (immediate operands, discarded increments,
// copy propagation, dead definitions, redundant extensions) must compute what
// the same code computes through variables.

#include <limits.h>
#include <setjmp.h>
#include <stdint.h>

// A value the optimiser cannot see through.
long opaque(long v) {
    volatile long t = v;
    return t;
}

static const long long vals[] = {
    0,       1,          4,         5,           6,
    126,     127,        128,       254,         255,
    256,     -1,         -2,        -128,        -129,
    32767,   32768,      65535,     INT_MAX,     INT_MIN,
    4294967295LL, 4294967296LL, LLONG_MAX, LLONG_MIN};
#define NVALS (int)(sizeof vals / sizeof vals[0])

// A literal on either side against the same value held in a variable of the
// type the comparison is done in.
#define CHECK_CMP(T, X, K)                                                     \
    do {                                                                       \
        T x = (X);                                                             \
        typeof(x + (K)) xc = x;                                                \
        typeof(x + (K)) kc = (K);                                              \
        AssertEq(x < K, xc < kc);                                              \
        AssertEq(x <= K, xc <= kc);                                            \
        AssertEq(x > K, xc > kc);                                              \
        AssertEq(x >= K, xc >= kc);                                            \
        AssertEq(x == K, xc == kc);                                            \
        AssertEq(x != K, xc != kc);                                            \
        AssertEq(K < x, kc < xc);                                              \
        AssertEq(K <= x, kc <= xc);                                            \
        AssertEq(K > x, kc > xc);                                              \
        AssertEq(K >= x, kc >= xc);                                            \
        AssertEq(K == x, kc == xc);                                            \
        AssertEq(K != x, kc != xc);                                            \
    } while (0)

#define CMP_ALL_K(T, X)                                                        \
    do {                                                                       \
        CHECK_CMP(T, X, 0);                                                    \
        CHECK_CMP(T, X, 5);                                                    \
        CHECK_CMP(T, X, -1);                                                   \
        CHECK_CMP(T, X, 127);                                                  \
        CHECK_CMP(T, X, 255);                                                  \
        CHECK_CMP(T, X, 2147483647);                                           \
    } while (0)

#define CMP_TYPE(T)                                                            \
    for (int i = 0; i < NVALS; i++)                                            \
        CMP_ALL_K(T, (T)vals[i])

[[cccc::test]]
void test_compare_immediates_int(void) {
    CMP_TYPE(int);
}

[[cccc::test]]
void test_compare_immediates_unsigned(void) {
    CMP_TYPE(unsigned);
}

[[cccc::test]]
void test_compare_immediates_long(void) {
    CMP_TYPE(long);
}

[[cccc::test]]
void test_compare_immediates_unsigned_long(void) {
    CMP_TYPE(unsigned long);
}

[[cccc::test]]
void test_compare_immediates_narrow(void) {
    CMP_TYPE(signed char);
    CMP_TYPE(unsigned char);
    CMP_TYPE(short);
    CMP_TYPE(unsigned short);
}

[[cccc::test]]
void test_compare_unsigned_against_negative_literal(void) {
    unsigned u = 7;
    AssertEq(u < -1, 1);
    AssertEq(u > -1, 0);
    unsigned long ul = 7;
    AssertEq(ul < -1, 1);
    AssertEq(ul >= -1, 0);
    int i = -1;
    AssertEq(i < 0u, 0);
    AssertEq(i > 0u, 1);
    long l = -1;
    AssertEq(l < 0u, 1);
}

#define CHECK_ARITH(T, X, K)                                                   \
    do {                                                                       \
        T x = (X);                                                             \
        typeof(x + (K)) xc = x;                                                \
        typeof(x + (K)) kc = (K);                                              \
        AssertEq(x + K, xc + kc);                                              \
        AssertEq(K + x, kc + xc);                                              \
        AssertEq(x - K, xc - kc);                                              \
        AssertEq(K - x, kc - xc);                                              \
        AssertEq(x * K, xc * kc);                                              \
    } while (0)

#define ARITH_TYPE(T)                                                          \
    for (int i = 0; i < NVALS; i++) {                                          \
        CHECK_ARITH(T, (T)vals[i], 0);                                         \
        CHECK_ARITH(T, (T)vals[i], 1);                                         \
        CHECK_ARITH(T, (T)vals[i], -1);                                        \
        CHECK_ARITH(T, (T)vals[i], 100);                                       \
        CHECK_ARITH(T, (T)vals[i], 2147483647);                                \
    }

[[cccc::test]]
void test_add_sub_immediates(void) {
    ARITH_TYPE(int);
    ARITH_TYPE(unsigned);
    ARITH_TYPE(long);
    ARITH_TYPE(unsigned long);
    ARITH_TYPE(short);
    ARITH_TYPE(unsigned char);
}

[[cccc::test]]
void test_add_immediate_wraps_at_width(void) {
    int      a = INT_MAX;
    unsigned b = UINT_MAX;
    AssertEq(a + 1, INT_MIN);
    AssertEq(b + 1, 0u);
    AssertEq(b - -1, 0u);
    AssertEq((long)(a + 1), (long)INT_MIN);
    AssertEq((long)a + 1, 2147483648L);
}

typedef struct {
    int           i;
    unsigned char uc;
    short         s;
    float         f;
    double        d;
    unsigned      bf : 3;
    int          *p;
} Rec;

[[cccc::test]]
void test_discarded_increments(void) {
    signed char   c  = 126;
    unsigned char uc = 254;
    short         s  = 32766;
    unsigned      u  = 4294967294u;
    long          l  = LONG_MAX - 2;
    float         f  = 0.5f;
    double        d  = 0.25;
    int           ar[3] = {1, 2, 3};
    int          *p     = ar;
    Rec           r     = {.i = INT_MAX - 1, .uc = 255, .bf = 6, .p = ar};
    for (int n = 0; n < 3; n++) {
        c++;
        uc++;
        s++;
        u++;
        l++;
        f++;
        d++;
        p++;
        r.i++;
        r.uc++;
        r.bf++;
        r.p++;
        ar[1]++;
    }
    AssertEq(c, -127);
    AssertEq(uc, 1);
    AssertEq(s, -32767);
    AssertEq(u, 1u);
    AssertEq(l, LONG_MIN);
    AssertEq(f, 3.5f);
    AssertEq(d, 3.25);
    AssertEq(p - ar, 3);
    AssertEq(r.i, INT_MIN + 1);
    AssertEq(r.uc, 2);
    AssertEq(r.bf, 1u);
    AssertEq(r.p - ar, 3);
    AssertEq(ar[1], 5);
}

[[cccc::test]]
void test_discarded_decrements(void) {
    unsigned char uc = 1;
    int           i  = INT_MIN + 1;
    unsigned      u  = 1;
    long          l  = 2;
    double        d  = 1.5;
    for (int n = 0; n < 3; n++) {
        uc--;
        i--;
        u--;
        l--;
        --d;
    }
    AssertEq(uc, 254);
    AssertEq(i, INT_MAX - 1);
    AssertEq(u, 4294967294u);
    AssertEq(l, -1);
    AssertEq(d, -1.5);
}

[[cccc::test]]
void test_postfix_value_is_kept_when_used(void) {
    int  x = 5;
    int  y = x++;
    int  z = x--;
    int *p = &x;
    int  w = (*p)++;
    AssertEq(y, 5);
    AssertEq(z, 6);
    AssertEq(w, 5);
    AssertEq(x, 6);
    unsigned char uc = 255;
    int           v  = uc++;
    AssertEq(v, 255);
    AssertEq(uc, 0);
    int a = 0, b = 10;
    for (int i = 0; i < 4; i++, a++, b--) {}
    AssertEq(a, 4);
    AssertEq(b, 6);
    int t = (a++, b++, a + b);
    AssertEq(t, 5 + 7);
}

[[cccc::test]]
void test_volatile_increments_keep_their_accesses(void) {
    volatile int v = 0;
    for (int i = 0; i < 10; i++)
        v++;
    AssertEq(v, 10);
}

// Copies must not read a source that changed after the copy.
long copy_then_modify(long x) {
    long a = x;
    long b = a;
    x      = 99;
    long c = b;
    a      = 7;
    return c * 1000 + x + a;
}

[[cccc::test]]
void test_copy_not_propagated_past_redefinition(void) {
    AssertEq(copy_then_modify(3), 3 * 1000 + 99 + 7);
}

long swap_chain(long a, long b) {
    long t = a;
    a      = b;
    b      = t;
    long u = a;
    a      = b;
    b      = u;
    return a * 10 + b;
}

[[cccc::test]]
void test_swaps(void) {
    AssertEq(swap_chain(1, 2), 12);
}

long loop_with_copies(long n) {
    long s = 0, prev = 0, prev2 = 0;
    for (long i = 0; i < n; i++) {
        long t = i;
        s     += prev + prev2;
        prev2  = prev;
        prev   = t;
    }
    return s;
}

[[cccc::test]]
void test_loop_carried_copies(void) {
    long expect = 0, prev = 0, prev2 = 0;
    for (long i = 0; i < 20; i++) {
        long t = opaque(i);
        expect += prev + prev2;
        prev2   = prev;
        prev    = t;
    }
    AssertEq(loop_with_copies(20), expect);
}

double fcopies(double x) {
    double a = x;
    double b = a;
    x        = 3.0;
    double c = b;
    a        = 0.5;
    return c * 100 + x + a;
}

[[cccc::test]]
void test_float_copies(void) {
    AssertEq(fcopies(2.0), 2.0 * 100 + 3.0 + 0.5);
}

// A temporary written in both arms of a join and read after it.
long join_value(long a, long b) {
    long r = (a > b ? a - b : b - a) + (a && b ? 1 : 0) + (a || b);
    return r;
}

[[cccc::test]]
void test_joins_keep_their_temporaries(void) {
    AssertEq(join_value(5, 3), 2 + 1 + 1);
    AssertEq(join_value(3, 5), 2 + 1 + 1);
    AssertEq(join_value(0, 4), 4 + 0 + 1);
    AssertEq(join_value(0, 0), 0);
}

int dense_switch(int v) {
    int r = 0;
    switch (v) {
        case 0: r = 10; break;
        case 1: r = 11;
        case 2: r += 12; break;
        case 3: r = 13; break;
        case 4: r = 14; break;
        case 5: r = 15; break;
        default: r = -1;
    }
    return r + v;
}

[[cccc::test]]
void test_switch_tables(void) {
    int total = 0;
    for (int i = -1; i < 8; i++)
        total += dense_switch(opaque(i));
    AssertEq(total, (-1 - 1) + 10 + (11 + 12 + 1) + (12 + 2) + 16 + 18 + 20 + (-1 + 6) + (-1 + 7));
}

long goto_loop(long n) {
    long i = 0, s = 0;
again:
    if (i >= n)
        goto done;
    s += i * 2;
    i++;
    goto again;
done:
    return s;
}

[[cccc::test]]
void test_goto(void) {
    AssertEq(goto_loop(10), 90);
}

long computed_goto(long n) {
    static void *targets[] = {&&add, &&dbl, &&stop};
    long         acc = 1;
    long         pc  = 0;
    for (;;) {
        goto *targets[pc < 3 ? pc : 2];
    add:
        acc += n;
        pc++;
        continue;
    dbl:
        acc *= 2;
        pc++;
        continue;
    stop:
        return acc;
    }
}

[[cccc::test]]
void test_computed_goto(void) {
    AssertEq(computed_goto(4), (1 + 4) * 2);
}

[[cccc::test]]
void test_extension_wraps(void) {
    int i = -1;
    AssertEq((long)(unsigned)i, 4294967295L);
    AssertEq((unsigned)(int)-5, 4294967291u);
    unsigned char a = 200, b = 100;
    AssertEq((unsigned char)(a + b), 44);
    AssertEq(a + b, 300);
    signed char sc = 100;
    sc             = sc + sc;
    AssertEq(sc, -56);
    short s = 30000;
    s       = (short)(s + s);
    AssertEq(s, -5536);
    int m = INT_MAX;
    m     = m + 1;
    m     = m - 1;
    AssertEq(m, INT_MAX);
    unsigned w = 0;
    w          = w - 1;
    w          = w + 2;
    AssertEq(w, 1u);
    int big = 65536;
    AssertEq(big * big, 0);
}

[[cccc::test]]
void test_loads_then_extensions(void) {
    signed char sb[2]   = {-1, 127};
    short       sh[2]   = {-2, 32767};
    int         iw[2]   = {INT_MIN, INT_MAX};
    unsigned char ub[2] = {255, 1};
    AssertEq((unsigned)sb[0], 4294967295u);
    AssertEq((unsigned char)sb[0], 255);
    AssertEq((unsigned short)sh[0], 65534);
    AssertEq((unsigned)iw[0], 2147483648u);
    AssertEq((long)ub[0] + sb[0], 254);
    AssertEq((int)(signed char)ub[0], -1);
}

long args_through_copies(long a, long b, long c) {
    long x = a, y = b, z = c;
    a = z;
    c = x;
    return opaque(x) * 100 + opaque(y) * 10 + opaque(z) + a * 1000 + c * 10000;
}

[[cccc::test]]
void test_registers_across_calls(void) {
    AssertEq(args_through_copies(1, 2, 3), 100 + 20 + 3 + 3000 + 10000);
}

long fib(long n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

[[cccc::test]]
void test_recursion(void) {
    AssertEq(fib(15), 610);
}

static jmp_buf pj;

[[cccc::test]]
void test_setjmp_function_is_untouched(void) {
    volatile int v = 0;
    int          i;
    if (setjmp(pj) == 0) {
        for (i = 0; i < 5; i++)
            v++;
        longjmp(pj, 1);
    }
    AssertEq(v, 5);
}

long nested_loops(long n) {
    long s = 0;
    for (long i = 0; i < n; i++) {
        for (long j = i; j < n; j++) {
            if ((i + j) % 3 == 0)
                continue;
            if (j > i + 6)
                break;
            s += i * j;
        }
    }
    return s;
}

[[cccc::test]]
void test_nested_loops(void) {
    long expect = 0;
    for (long i = 0; i < opaque(12); i++)
        for (long j = i; j < opaque(12); j++) {
            if ((i + j) % 3 == 0)
                continue;
            if (j > i + 6)
                break;
            expect += i * j;
        }
    AssertEq(nested_loops(12), expect);
}

[[cccc::test]]
void test_bool_and_bitfield_results(void) {
    int  a = 3, b = 3;
    bool r = a == b;
    AssertEq(r, true);
    r = a != 3;
    AssertEq(r, false);
    Rec x = {.bf = 7};
    x.bf  = x.bf + 1;
    AssertEq(x.bf, 0u);
}

// Constant expressions fold to what the same operations compute at run time.
#define FOLD_BIN(T, A, B)                                                      \
    do {                                                                       \
        T x = (T)opaque(A), y = (T)opaque(B);                                  \
        AssertEq((T)(A) + (T)(B), x + y);                                      \
        AssertEq((T)(A) - (T)(B), x - y);                                      \
        AssertEq((T)(A) * (T)(B), x * y);                                      \
        AssertEq((T)(A) & (T)(B), x & y);                                      \
        AssertEq((T)(A) | (T)(B), x | y);                                      \
        AssertEq((T)(A) ^ (T)(B), x ^ y);                                      \
        AssertEq((T)(A) < (T)(B), x < y);                                      \
        AssertEq((T)(A) <= (T)(B), x <= y);                                    \
        AssertEq((T)(A) == (T)(B), x == y);                                    \
        AssertEq((T)(A) != (T)(B), x != y);                                    \
        AssertEq(-(T)(A), -x);                                                 \
        AssertEq(~(T)(A), ~x);                                                 \
        AssertEq(!(T)(A), !x);                                                 \
    } while (0)

#define FOLD_DIV(T, A, B)                                                      \
    do {                                                                       \
        T x = (T)opaque(A), y = (T)opaque(B);                                  \
        AssertEq((T)(A) / (T)(B), x / y);                                      \
        AssertEq((T)(A) % (T)(B), x % y);                                      \
    } while (0)

#define FOLD_SHIFT(T, A, S)                                                    \
    do {                                                                       \
        T x = (T)opaque(A);                                                    \
        int s = (int)opaque(S);                                                \
        AssertEq((T)(A) << (S), x << s);                                       \
        AssertEq((T)(A) >> (S), x >> s);                                       \
    } while (0)

#define FOLD_PAIRS(T)                                                          \
    do {                                                                       \
        FOLD_BIN(T, 5, 3);                                                     \
        FOLD_BIN(T, -7, 2);                                                    \
        FOLD_BIN(T, 1000000, 3000);                                            \
        FOLD_BIN(T, 255, 1);                                                   \
        FOLD_BIN(T, -1, -1);                                                   \
        FOLD_BIN(T, 127, 1);                                                   \
        FOLD_BIN(T, 2147483647, 1);                                            \
        FOLD_BIN(T, -2147483647 - 1, 1);                                       \
        FOLD_BIN(T, 0, 0);                                                     \
        FOLD_DIV(T, 100, 7);                                                   \
        FOLD_DIV(T, -100, 7);                                                  \
        FOLD_DIV(T, 100, -7);                                                  \
        FOLD_DIV(T, 7, 100);                                                   \
        FOLD_SHIFT(T, 1, 3);                                                   \
        FOLD_SHIFT(T, -16, 2);                                                 \
        FOLD_SHIFT(T, 255, 7);                                                 \
    } while (0)

[[cccc::test]]
void test_constant_folding_matches_run_time(void) {
    FOLD_PAIRS(int);
    FOLD_PAIRS(unsigned);
    FOLD_PAIRS(long);
    FOLD_PAIRS(unsigned long);
    FOLD_PAIRS(short);
    FOLD_PAIRS(unsigned short);
    FOLD_PAIRS(signed char);
    FOLD_PAIRS(unsigned char);
}

[[cccc::test]]
void test_folding_leaves_traps_and_wide_shifts_alone(void) {
    int zero = (int)opaque(0);
    AssertEq(zero, 0);
    AssertEq(10 / (zero + 5), 2);
    AssertEq(10 % (zero + 4), 2);
    AssertEq(1 << (zero + 31) < 0, 1);
}
