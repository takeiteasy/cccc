// CCCC_FLAGS: --testing -B
// CCCC_NATIVE_SKIP: exercises the VM's fused compare-and-branch opcodes
// The same cases under bounds checks, where the branches stay unfused.
// unsigned, must agree with the same comparison taken as a value, which is not
// fused. The constants straddle the 24-bit immediate limit.

#include <limits.h>

// clang-format off
static const long long vals[] = {
    0,        1,         -1,        5,          127,      128,
    255,      256,       8388607,   8388608,    -8388608, -8388609,
    INT_MAX,  INT_MIN,   4294967295LL, LLONG_MAX, LLONG_MIN};
#define NV (int)(sizeof vals / sizeof vals[0])

#define FOR_OPS(M, ...)                                                        \
    M(lt, <, __VA_ARGS__) M(le, <=, __VA_ARGS__) M(gt, >, __VA_ARGS__)        \
    M(ge, >=, __VA_ARGS__) M(eq, ==, __VA_ARGS__) M(ne, !=, __VA_ARGS__)

#define FOR_KS(M, ...)                                                         \
    M(k0, 0, __VA_ARGS__) M(km1, -1, __VA_ARGS__) M(k5, 5, __VA_ARGS__)       \
    M(kmax, 8388607, __VA_ARGS__) M(kover, 8388608, __VA_ARGS__)               \
    M(kmin, -8388608, __VA_ARGS__) M(kunder, -8388609, __VA_ARGS__)

#define RR_DEF(OPN, OP, S, T)                                                  \
    static int rr_if_##S##_##OPN(T a, T b) { if (a OP b) return 1; return 0; } \
    static int rr_not_##S##_##OPN(T a, T b) {                                  \
        if (!(a OP b)) return 0;                                               \
        return 1;                                                              \
    }                                                                          \
    static int rr_val_##S##_##OPN(T a, T b) { int r = a OP b; return r; }      \
    static int rr_do_##S##_##OPN(T a, T b) {                                   \
        int n = 0;                                                             \
        do { n++; } while (n < 3 && (a OP b));                                 \
        return n;                                                              \
    }

#define RI_DEF_K(KN, K, OPN, OP, S, T)                                         \
    static int ri_if_##S##_##OPN##_##KN(T a) {                                 \
        if (a OP(T)(K)) return 1;                                              \
        return 0;                                                              \
    }                                                                          \
    static int ri_left_##S##_##OPN##_##KN(T a) {                               \
        if ((T)(K) OP a) return 1;                                             \
        return 0;                                                              \
    }                                                                          \
    static int ri_val_##S##_##OPN##_##KN(T a) { int r = a OP(T)(K); return r; }\
    static int ri_lval_##S##_##OPN##_##KN(T a) {                               \
        int r = (T)(K) OP a;                                                   \
        return r;                                                              \
    }
#define RI_DEF(OPN, OP, S, T) FOR_KS(RI_DEF_K, OPN, OP, S, T)

#define DEFINE_TYPE(S, T)                                                      \
    FOR_OPS(RR_DEF, S, T)                                                      \
    FOR_OPS(RI_DEF, S, T)

DEFINE_TYPE(i32, int)
DEFINE_TYPE(u32, unsigned)
DEFINE_TYPE(i64, long long)
DEFINE_TYPE(u64, unsigned long long)

#define RR_CHECK(OPN, OP, S, T)                                                \
    for (int i = 0; i < NV; i++)                                               \
        for (int j = 0; j < NV; j++) {                                         \
            T   a = (T)vals[i], b = (T)vals[j];                                \
            int v = rr_val_##S##_##OPN(a, b);                                  \
            AssertEq(rr_if_##S##_##OPN(a, b), v);                              \
            AssertEq(rr_not_##S##_##OPN(a, b), v);                             \
            AssertEq(rr_do_##S##_##OPN(a, b), v ? 3 : 1);                      \
        }

#define RI_CHECK_K(KN, K, OPN, OP, S, T)                                       \
    for (int i = 0; i < NV; i++) {                                             \
        T a = (T)vals[i];                                                      \
        AssertEq(ri_if_##S##_##OPN##_##KN(a), ri_val_##S##_##OPN##_##KN(a));   \
        AssertEq(ri_left_##S##_##OPN##_##KN(a),                                \
                 ri_lval_##S##_##OPN##_##KN(a));                               \
    }
#define RI_CHECK(OPN, OP, S, T) FOR_KS(RI_CHECK_K, OPN, OP, S, T)

[[cccc::test]] void int_registers(void) { FOR_OPS(RR_CHECK, i32, int) }
[[cccc::test]] void int_immediates(void) { FOR_OPS(RI_CHECK, i32, int) }
[[cccc::test]] void unsigned_registers(void) { FOR_OPS(RR_CHECK, u32, unsigned) }
[[cccc::test]] void unsigned_immediates(void) { FOR_OPS(RI_CHECK, u32, unsigned) }
[[cccc::test]] void long_registers(void) { FOR_OPS(RR_CHECK, i64, long long) }
[[cccc::test]] void long_immediates(void) { FOR_OPS(RI_CHECK, i64, long long) }
[[cccc::test]] void ulong_registers(void) {
    FOR_OPS(RR_CHECK, u64, unsigned long long)
}
[[cccc::test]] void ulong_immediates(void) {
    FOR_OPS(RI_CHECK, u64, unsigned long long)
}

// A comparison used as a value and as a branch condition stays a value.
[[cccc::test]] void value_and_branch(void) {
    int hits = 0;
    for (int i = 0; i < NV; i++) {
        long long a = vals[i];
        int       r = a < 5;
        if (a < 5)
            hits += 1;
        AssertEq(r, a < 5);
    }
    AssertEq(hits, 7);
}

// The result of the comparison is read again after the branch.
[[cccc::test]] void result_read_after_branch(void) {
    int r = 0, hits = 0;
    for (int i = 0; i < NV; i++) {
        long long a = vals[i];
        r           = a < 5;
        if (r)
            hits++;
        hits += r;
    }
    AssertEq(hits, 14);
    AssertEq(r, vals[NV - 1] < 5);
}
// clang-format on
