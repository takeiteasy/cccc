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
void test_setjmp_function_with_loop(void) {
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

// ---- Facts that cross a branch ----

long tern_narrow(long v, long k) {
    return (int)(k ? (long)(short)v : (long)(int)v);
}

long tern_raw_arm(long v, long k) {
    return (int)(k ? (long)(int)v : v);
}

long tern_zero_extended(long v, long k) {
    return (unsigned char)(k ? (long)(unsigned char)v : v);
}

long logical_chain(long v, long w) {
    return (int)((v && w) ? (long)(signed char)v : w);
}

[[cccc::test]]
void test_extension_facts_meet_at_joins(void) {
    for (int i = 0; i < NVALS; i++) {
        long v = (long)opaque(vals[i]);
        for (long k = 0; k < 2; k++) {
            AssertEq(tern_narrow(v, k), k ? (long)(short)v : (long)(int)v);
            AssertEq(tern_raw_arm(v, k), (long)(int)v);
            AssertEq(tern_zero_extended(v, k), (long)(unsigned char)v);
        }
        for (int j = 0; j < NVALS; j++) {
            long w = (long)opaque(vals[j]);
            AssertEq(logical_chain(v, w),
                     (v && w) ? (long)(signed char)v : (long)(int)w);
        }
    }
}

long loop_wrap_int(long n, long seed) {
    long x = (int)seed;
    for (long i = 0; i < n; i++)
        x = (int)(x * 31 + i);
    return x;
}

long loop_wrap_int_ref(long n, long seed) {
    long x = (int)opaque(seed);
    for (long i = 0; i < n; i++)
        x = (int)opaque(x * 31 + i);
    return x;
}

long loop_wrap_byte(long n) {
    unsigned char c = 250;
    long          s = 0;
    for (long i = 0; i < n; i++) {
        c += 3;
        s += c;
    }
    return s;
}

[[cccc::test]]
void test_extension_facts_in_loops(void) {
    for (int i = 0; i < NVALS; i++)
        AssertEq(loop_wrap_int(40, (long)vals[i]),
                 loop_wrap_int_ref(40, (long)vals[i]));
    long s = 0, c = 250;
    for (long i = 0; i < 300; i++) {
        c  = (c + 3) % 256;
        s += c;
    }
    AssertEq(loop_wrap_byte(300), s);
}

long and_mask31(long v) {
    return (int)(v & 0x7FFFFFFF);
}
long and_mask32(long v) {
    return (int)(v & 0xFFFFFFFF);
}
long and_byte(long v) {
    return (signed char)(v & 0xFF);
}
long and_mask_negative(long v, long m) {
    return (int)(v & (int)m);
}
long or_ints(long a, long b) {
    return (int)((int)a | (int)b);
}
long xor_bytes(long a, long b) {
    return (int)((signed char)a ^ (signed char)b);
}

[[cccc::test]]
void test_bitwise_extension_facts(void) {
    for (int i = 0; i < NVALS; i++) {
        long v = (long)opaque(vals[i]);
        AssertEq(and_mask31(v), v & 0x7FFFFFFF);
        AssertEq(and_mask32(v), (long)(int)v);
        AssertEq(and_byte(v), (long)(signed char)v);
        for (int j = 0; j < NVALS; j++) {
            long m = (long)opaque(vals[j]);
            AssertEq(and_mask_negative(v, m),
                     (long)(int)opaque(v & (long)(int)opaque(m)));
            AssertEq(or_ints(v, m), (long)(int)opaque((long)(int)opaque(v) |
                                                      (long)(int)opaque(m)));
            AssertEq(xor_bytes(v, m),
                     (long)(int)opaque((long)(signed char)opaque(v) ^
                                       (long)(signed char)opaque(m)));
        }
    }
}

long zx_branch(long v) {
    return (unsigned char)v ? 1 : 0;
}
long zx2_branch(long v) {
    return (unsigned short)v ? 1 : 0;
}
long sx_branch(long v) {
    return (signed char)v ? 1 : 0;
}
long zx_branch_char(signed char c) {
    return (unsigned char)c ? 1 : 0;
}

long zx_branch_live(long v) {
    unsigned char c = v;
    if (c)
        return c;
    return -1;
}

[[cccc::test]]
void test_extension_before_a_branch(void) {
    for (int i = 0; i < NVALS; i++) {
        long v = (long)opaque(vals[i]);
        AssertEq(zx_branch(v), (v & 0xFF) != 0);
        AssertEq(zx2_branch(v), (v & 0xFFFF) != 0);
        AssertEq(sx_branch(v), (v & 0xFF) != 0);
        AssertEq(zx_branch_char((signed char)v), (v & 0xFF) != 0);
        AssertEq(zx_branch_live(v), (v & 0xFF) ? (v & 0xFF) : -1);
    }
}

long switch_ext(long v, int sel) {
    long x = (int)v;
    switch (sel) {
        case 0:
            break;
        case 1:
            x = v;
            break;
        case 2:
            x = (short)v;
            break;
        case 3:
            x = (unsigned char)v;
            break;
        default:
            x += 1;
            break;
    }
    return (int)x;
}

[[cccc::test]]
void test_extension_facts_into_switch_targets(void) {
    for (int i = 0; i < NVALS; i++) {
        long v = (long)opaque(vals[i]);
        AssertEq(switch_ext(v, 0), (long)(int)v);
        AssertEq(switch_ext(v, 1), (long)(int)v);
        AssertEq(switch_ext(v, 2), (long)(short)v);
        AssertEq(switch_ext(v, 3), (long)(unsigned char)v);
        AssertEq(switch_ext(v, 4), (long)(int)((long)(int)v + 1));
    }
}

// ---- Computed goto ----

enum { OP_ADD, OP_DOUBLE, OP_NEG, OP_BYTE, OP_END };

long interp_goto(const unsigned char *code, long arg) {
    static void *tbl[] = {&&add, &&dbl, &&neg, &&byte, &&end};
    long         acc   = arg;
    long         steps = 0;
    goto        *tbl[*code++];
add:
    acc += 3;
    steps++;
    goto *tbl[*code++];
dbl:
    acc = (int)(acc * 2);
    steps++;
    goto *tbl[*code++];
neg:
    acc = -acc;
    steps++;
    goto *tbl[*code++];
byte:
    acc = (signed char)acc;
    steps++;
    goto *tbl[*code++];
end:
    return acc * 100 + steps;
}

long interp_switch(const unsigned char *code, long arg) {
    long acc = arg, steps = 0;
    for (;; code++) {
        switch (*code) {
            case OP_ADD:
                acc = (long)opaque(acc + 3);
                break;
            case OP_DOUBLE:
                acc = (int)opaque(acc * 2);
                break;
            case OP_NEG:
                acc = (long)opaque(-acc);
                break;
            case OP_BYTE:
                acc = (signed char)opaque(acc);
                break;
            default:
                return acc * 100 + steps;
        }
        steps++;
    }
}

long local_label_goto(long v, int n) {
    void *where = n ? &&again : &&done;
    long  x     = (int)v;
    goto *where;
again:
    x     = (short)(x + n);
    where = &&done;
    goto *where;
done:
    return (int)x;
}

[[cccc::test]]
void test_computed_goto_dispatch(void) {
    static const unsigned char progs[][8] = {
        {OP_END},
        {OP_ADD, OP_DOUBLE, OP_END},
        {OP_DOUBLE, OP_DOUBLE, OP_DOUBLE, OP_NEG, OP_END},
        {OP_BYTE, OP_ADD, OP_BYTE, OP_NEG, OP_DOUBLE, OP_END},
        {OP_ADD, OP_ADD, OP_ADD, OP_ADD, OP_BYTE, OP_DOUBLE, OP_NEG, OP_END},
    };
    for (int p = 0; p < 5; p++)
        for (int i = 0; i < NVALS; i++) {
            long v = (long)opaque(vals[i]);
            AssertEq(interp_goto(progs[p], v), interp_switch(progs[p], v));
        }
    for (int i = 0; i < NVALS; i++) {
        long v = (long)opaque(vals[i]);
        AssertEq(local_label_goto(v, 0), (long)(int)v);
        AssertEq(local_label_goto(v, 5), (long)(short)((long)(int)v + 5));
    }
}

// ---- setjmp and cleanup ----

static jmp_buf sj;

static void thrower(long v) {
    longjmp(sj, (int)(v & 0x7FFF) + 1);
}

long setjmp_flow(long v) {
    volatile long seen   = 0;
    long          before = (int)v;
    int           r      = setjmp(sj);
    if (r == 0) {
        seen = before + 1;
        thrower(v);
    }
    return seen * 1000 + r + (int)before;
}

[[cccc::test]]
void test_setjmp_keeps_values_across_longjmp(void) {
    for (int i = 0; i < NVALS; i++) {
        long v = (long)opaque(vals[i]);
        long b = (int)v;
        AssertEq(setjmp_flow(v), (b + 1) * 1000 + ((v & 0x7FFF) + 1) + b);
    }
}

static int cl_log[8];
static int cl_n;

static void cl_rec(int *p) {
    cl_log[cl_n++] = *p;
}

long cleanup_flow(long v, int mode) {
    __attribute__((cleanup(cl_rec))) int a = (int)v;
    long                                 x = (int)v;
    if (mode == 0)
        return x;
    {
        __attribute__((cleanup(cl_rec))) int b = (int)(v >> 3);
        if (mode == 1)
            return (int)(x + b);
        x = v;
    }
    return (int)x;
}

[[cccc::test]]
void test_cleanup_function_flow(void) {
    for (int i = 0; i < NVALS; i++) {
        long v = (long)opaque(vals[i]);
        int  a = (int)v, b = (int)(v >> 3);
        cl_n = 0;
        AssertEq(cleanup_flow(v, 0), (long)a);
        AssertEq(cl_n, 1);
        AssertEq(cl_log[0], a);
        cl_n = 0;
        AssertEq(cleanup_flow(v, 1), (long)(int)((long)a + b));
        AssertEq(cl_n, 2);
        AssertEq(cl_log[0], b);
        AssertEq(cl_log[1], a);
        cl_n = 0;
        AssertEq(cleanup_flow(v, 2), (long)a);
        AssertEq(cl_n, 2);
        AssertEq(cl_log[0], b);
        AssertEq(cl_log[1], a);
    }
}

// ---- Value ranges narrowed by a branch ----
// Each function does what a loop index does when the bound sits at an `int`
// limit; the reference runs the same loop in `long`, where nothing wraps.

typedef struct {
    long lo, hi;
} Span;

#define N_SPANS(a) (int)(sizeof(a) / sizeof((a)[0]))

long loop_lt(int lo, int hi) {
    long n = 0;
    int  j;
    for (j = lo; j < hi; j++) {
        opaque(j);
        n++;
    }
    return (long)j * 8 + n;
}

long loop_lt_ref(long lo, long hi) {
    long n = 0, j;
    for (j = lo; j < hi; j++)
        n++;
    return j * 8 + n;
}

long loop_le(int lo, int hi) {
    long n = 0;
    int  j;
    for (j = lo; j <= hi; j++) {
        opaque(j);
        n++;
    }
    return (long)j * 8 + n;
}

long loop_le_ref(long lo, long hi) {
    long n = 0, j;
    for (j = lo; j <= hi; j++)
        n++;
    return j * 8 + n;
}

long loop_gt(int lo, int hi) {
    long n = 0;
    int  j;
    for (j = hi; j > lo; j--) {
        opaque(j);
        n++;
    }
    return (long)j * 8 + n;
}

long loop_gt_ref(long lo, long hi) {
    long n = 0, j;
    for (j = hi; j > lo; j--)
        n++;
    return j * 8 + n;
}

long loop_ge(int lo, int hi) {
    long n = 0;
    int  j;
    for (j = hi; j >= lo; j--) {
        opaque(j);
        n++;
    }
    return (long)j * 8 + n;
}

long loop_ge_ref(long lo, long hi) {
    long n = 0, j;
    for (j = hi; j >= lo; j--)
        n++;
    return j * 8 + n;
}

long loop_ult(unsigned lo, unsigned hi) {
    long     n = 0;
    unsigned j;
    for (j = lo; j < hi; j++) {
        opaque(j);
        n++;
    }
    return (long)j * 8 + n;
}

long loop_ult_ref(unsigned long lo, unsigned long hi) {
    long          n = 0;
    unsigned long j;
    for (j = lo; j < hi; j++)
        n++;
    return (long)j * 8 + n;
}

[[cccc::test]]
void test_range_signed_loops_at_int_limits(void) {
    static const Span lt[] = {{INT_MAX - 3L, INT_MAX},
                              {INT_MIN, INT_MIN + 3L},
                              {-2, 2},
                              {5, 5},
                              {INT_MAX, INT_MAX},
                              {3, 1}};
    for (int i = 0; i < N_SPANS(lt); i++)
        AssertEq(loop_lt((int)opaque(lt[i].lo), (int)opaque(lt[i].hi)),
                 loop_lt_ref(lt[i].lo, lt[i].hi));

    static const Span le[] = {
        {INT_MAX - 3L, INT_MAX - 1L}, {INT_MIN, INT_MIN + 2L}, {-2, 1}, {5, 4}};
    for (int i = 0; i < N_SPANS(le); i++)
        AssertEq(loop_le((int)opaque(le[i].lo), (int)opaque(le[i].hi)),
                 loop_le_ref(le[i].lo, le[i].hi));

    static const Span gt[] = {{INT_MIN, INT_MIN + 3L},
                              {INT_MAX - 3L, INT_MAX},
                              {-2, 2},
                              {5, 5},
                              {INT_MIN, INT_MIN}};
    for (int i = 0; i < N_SPANS(gt); i++)
        AssertEq(loop_gt((int)opaque(gt[i].lo), (int)opaque(gt[i].hi)),
                 loop_gt_ref(gt[i].lo, gt[i].hi));

    static const Span ge[] = {
        {INT_MIN + 1L, INT_MIN + 3L}, {INT_MAX - 3L, INT_MAX}, {-2, 2}, {5, 4}};
    for (int i = 0; i < N_SPANS(ge); i++)
        AssertEq(loop_ge((int)opaque(ge[i].lo), (int)opaque(ge[i].hi)),
                 loop_ge_ref(ge[i].lo, ge[i].hi));
}

[[cccc::test]]
void test_range_unsigned_loop_at_uint_limit(void) {
    static const Span ult[] = {
        {UINT_MAX - 3L, UINT_MAX}, {0, 3}, {5, 5}, {3, 1}};
    for (int i = 0; i < N_SPANS(ult); i++)
        AssertEq(
            loop_ult((unsigned)opaque(ult[i].lo), (unsigned)opaque(ult[i].hi)),
            loop_ult_ref((unsigned long)ult[i].lo, (unsigned long)ult[i].hi));
}

// The index is replaced after the compare that bounded it.
long redefined_in_body(int hi, long v) {
    int j = 0;
    for (int k = 0; k < 2; k++) {
        if (j < hi) {
            j = (int)opaque(v);
            j++;
        }
    }
    return j;
}

long redefined_in_body_ref(long hi, long v) {
    long j = 0;
    for (int k = 0; k < 2; k++) {
        if (j < hi) {
            j = (int)v;
            j = (int)(j + 1);
        }
    }
    return j;
}

[[cccc::test]]
void test_range_index_redefined_after_compare(void) {
    static const long his[] = {1, 5, INT_MIN, INT_MAX};
    static const long vs[]  = {INT_MAX, INT_MIN, 0, -1, 4294967296L + 5};
    for (int a = 0; a < N_SPANS(his); a++)
        for (int b = 0; b < N_SPANS(vs); b++)
            AssertEq(redefined_in_body((int)opaque(his[a]), opaque(vs[b])),
                     redefined_in_body_ref(his[a], vs[b]));
}

// The compare is used in a later block, after its result may have changed.
long flag_after_join(int j, int hi, long flag) {
    for (int k = 0; k < 1; k++) {
        int c = j < hi;
        if (opaque(flag))
            c = 1;
        if (c)
            j++;
    }
    return j;
}

long flag_after_join_ref(long j, long hi, long flag) {
    long c = j < hi || flag;
    return c ? (int)(j + 1) : j;
}

// An operand of the compare is replaced before the branch.
long operand_replaced_before_branch(int j, int hi) {
    for (int k = 0; k < 1; k++) {
        int c = j < hi;
        j     = j ^ INT_MAX;
        if (c)
            j++;
    }
    return j;
}

long operand_replaced_before_branch_ref(long j, long hi) {
    long c = j < hi;
    j      = (int)(j ^ INT_MAX);
    return c ? (int)(j + 1) : j;
}

[[cccc::test]]
void test_range_compare_does_not_outlive_its_operands(void) {
    static const long js[]  = {0, 1, -1, INT_MAX, INT_MIN};
    static const long his[] = {1, 0, INT_MAX, INT_MIN};
    for (int a = 0; a < N_SPANS(js); a++)
        for (int b = 0; b < N_SPANS(his); b++) {
            for (long flag = 0; flag < 2; flag++)
                AssertEq(flag_after_join((int)opaque(js[a]),
                                         (int)opaque(his[b]), flag),
                         flag_after_join_ref(js[a], his[b], flag));
            AssertEq(operand_replaced_before_branch((int)opaque(js[a]),
                                                    (int)opaque(his[b])),
                     operand_replaced_before_branch_ref(js[a], his[b]));
        }
}

// An unsigned order on operands that may be negative is not the signed one.
long unsigned_order_on_signed(int j, int n) {
    for (int k = 0; k < 1; k++) {
        if ((unsigned long)j < (unsigned long)n)
            j++;
    }
    return j;
}

[[cccc::test]]
void test_range_unsigned_order_on_negative_operands(void) {
    static const long js[] = {INT_MAX, 0, -1, 5};
    static const long ns[] = {-1, 0, 5, INT_MIN};
    for (int a = 0; a < N_SPANS(js); a++)
        for (int b = 0; b < N_SPANS(ns); b++) {
            bool lt   = (unsigned long)js[a] < (unsigned long)ns[b];
            long want = lt ? (long)(int)(js[a] + 1) : js[a];
            AssertEq(unsigned_order_on_signed((int)opaque(js[a]),
                                              (int)opaque(ns[b])),
                     want);
        }
}

// A throw out of the branch that bounded the index does not leave the bound
// behind on the path that lands after setjmp.
static jmp_buf range_jb;

static void range_throw(void) {
    longjmp(range_jb, 1);
}

long setjmp_after_bound(int j, int hi) {
    long n = 0;
    for (int k = 0; k < 3; k++) {
        if (setjmp(range_jb) == 0) {
            if (j < hi)
                range_throw();
        } else if (j < hi) {
            j++;
        }
        n++;
    }
    return (long)j * 8 + n;
}

long setjmp_after_bound_ref(long j, long hi) {
    for (int k = 0; k < 3; k++)
        if (j < hi)
            j++;
    return j * 8 + 3;
}

[[cccc::test]]
void test_range_setjmp_after_bound(void) {
    static const Span spans[] = {
        {INT_MAX - 1L, INT_MAX}, {0, 5}, {5, 0}, {INT_MIN, INT_MIN + 1L}};
    for (int i = 0; i < N_SPANS(spans); i++)
        AssertEq(setjmp_after_bound((int)opaque(spans[i].lo),
                                    (int)opaque(spans[i].hi)),
                 setjmp_after_bound_ref(spans[i].lo, spans[i].hi));
}

// The index is stepped once more after the loop that bounded it, where the
// bound no longer holds.
long step_after_loop_lt(int lo, int hi) {
    int j;
    for (j = lo; j < hi; j++)
        opaque(j);
    j++;
    return j;
}

long step_after_loop_gt(int lo, int hi) {
    int j;
    for (j = hi; j > lo; j--)
        opaque(j);
    j--;
    return j;
}

[[cccc::test]]
void test_range_step_after_loop_exit(void) {
    static const Span spans[] = {{INT_MAX - 2L, INT_MAX},
                                 {INT_MAX, INT_MAX},
                                 {INT_MIN, INT_MIN},
                                 {INT_MIN, INT_MIN + 2L},
                                 {0, 3},
                                 {3, 0}};
    for (int i = 0; i < N_SPANS(spans); i++) {
        long j = spans[i].lo;
        while (j < spans[i].hi)
            j++;
        AssertEq(step_after_loop_lt((int)opaque(spans[i].lo),
                                    (int)opaque(spans[i].hi)),
                 (long)(int)(j + 1));
        j = spans[i].hi;
        while (j > spans[i].lo)
            j--;
        AssertEq(step_after_loop_gt((int)opaque(spans[i].lo),
                                    (int)opaque(spans[i].hi)),
                 (long)(int)(j - 1));
    }
}
