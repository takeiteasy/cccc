// CCCC_FLAGS: --testing
// Consolidated suite: optimizer: constant fold, inline, CSE, compaction,
// pure/const, FMA,
//   elim-ext, fuse, O4 arith, tail-call, atomic ops
// Source tests: test_calln_callf_dead_elim, test_cse_const_calls,
// test_inline_dead,
//   test_inline_locals, test_inline_multistmt, test_inline_singlereturn,
//   test_inline_threshold, test_inline_void, test_optimizer_compaction,
//   test_optimizer_constant_fold, test_optimizer_indexed_ops,
//   test_optimizer_linear_statements, test_optimizer_scalar_promotion,
//   test_pure_const_attr, test_pure_dead_call_elim,
//   test_optimizer_fmadd_precision, test_optimizer_fmsub_precision,
//   test_optimizer_fnmsub_precision, test_optimizer_elim_ext,
//   test_optimizer_fuse_ops, test_optimizer_o4_fused_arith,
//   test_optimizer_tail_call, test_atomic_ops_o3

// [from test_calln_callf_dead_elim]
// Test dead-call elimination for CALLN (indirect calls).
// When an indirect call's result is unused and the function pointer type
// carries a pure/const annotation, the call dispatch is skipped at -O1+.
// Argument and function-pointer evaluation side effects must still execute.

// [[gnu::const]] annotated function

static [[gnu::const]] static int triple(int x) {
    return x * 3;
}

// [[gnu::pure]] annotated function

static [[gnu::pure]] static int double_it(int x) {
    return x * 2;
}

// Declare function pointer types that carry the annotations so CALLN
// dead-call elimination can see them via node->func_ty.
typedef int(__attribute__((const)) * const_fn_t)(int);
typedef int(__attribute__((pure)) * pure_fn_t)(int);

// [from test_cse_const_calls]
// Test CSE for [[gnu::const]] functions.
// When the same [[gnu::const]] function is called twice with the same
// argument value numbers (constants or unchanged locals), the second call
// is replaced by a register move at -O2+.

static [[gnu::const]] static int square(int x) {
    return x * x;
}

static [[gnu::const]] static int add2(int x, int y) {
    return x + y;
}

// [from test_inline_dead]
// Unused static inline functions produce no bytecode

static inline int unused(void) {
    return 42;
}

static inline int also_unused(int a, int b) {
    return a + b;
}

// [from test_inline_locals]
// Static inline functions with local variables are inlined

static inline int sum_to(int n) {
    int total = 0;
    for (int i = 1; i <= n; i++)
        total += i;
    return total;
}

static inline int fib(int n) {
    int a = 0, b = 1, tmp;
    for (int i = 0; i < n; i++) {
        tmp = a + b;
        a   = b;
        b   = tmp;
    }
    return a;
}

// [from test_inline_multistmt]
// Multi-statement static inline functions expand inline

static inline int clamp(int x, int lo, int hi) {
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

static inline int min(int a, int b) {
    if (a < b)
        return a;
    return b;
}

static inline int max(int a, int b) {
    if (a > b)
        return a;
    return b;
}

static inline int sign(int x) {
    if (x > 0)
        return 1;
    if (x < 0)
        return -1;
    return 0;
}

// [from test_inline_singlereturn]
// Single-return static inline callees expand inline (no CALL emitted)

static inline int add(int a, int b) {
    return a + b;
}

static inline int _inline_singlereturn_max(int a, int b) {
    return a < b ? b : a;
}

static inline int _inline_singlereturn_clamp(int x, int lo, int hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

// [from test_inline_threshold]
// Functions above the inline limit (default 20) fall back to normal CALL.
// This function has ~25 AST nodes, exceeding the default threshold.

static inline int big_add(int a, int b, int c, int d) {
    int r  = a;
    r     += b;
    r     += c;
    r     += d;
    return r;
}

// [from test_inline_void]
// Void static inline functions with side effects are inlined
static int counter = 0;

static inline void incr(int n) {
    counter += n;
}

// [from test_optimizer_compaction]
static int plus7(int x) {
    return x + 7;
}

int (*global_fp)(int) = plus7;

static int label_value(int x) {
    void *labels[] = {&&zero, &&one, &&two};
    goto *labels[x];
zero:
    return 10;
one:
    return 20;
two:
    return 30;
}

static int dense_switch(int x) {
    switch (x) {
        case 0 ... 63:
            return x + 1;
        default:
            return -1;
    }
}

// [from test_optimizer_constant_fold]
static int choose(int x) {
    if (x)
        return 5;
    return 9;
}

// [from test_optimizer_linear_statements]
// Regression for ticket #428: collect_promotion_candidates and
// restrict_derived_walk recursed into node->next while also recursing
// into children, making O2+ codegen O(2^N) in straight-line statement
// count.  A long sequence of sibling statements must compile quickly.

// [from test_optimizer_scalar_promotion]
static int bump(int x) {
    int y = x;
    for (int i = 0; i < 5; i++)
        y += i;
    return y;
}

// [from test_pure_const_attr]
// Test __attribute__((pure)) and __attribute__((const)) parsing
// Verifies GCC-style and C23-style syntax are accepted without errors.

// GCC-style declarations
__attribute__((pure)) int pure_decl(int x);
__attribute__((__pure__)) int pure_us_decl(int x);
__attribute__((const)) int const_decl(int x);
__attribute__((__const__)) int const_us_decl(int x);

// GCC-style definitions

static __attribute__((pure)) int pure_fn(int x) {
    return x * 2;
}

static __attribute__((const)) int const_fn(int x) {
    return x + 1;
}

// Both pure and const work on static functions
static __attribute__((pure)) int static_pure(int x) {
    return x - 1;
}
static __attribute__((const)) int static_const(int x) {
    return x * x;
}

// C23-style [[gnu::pure]] and [[gnu::const]]

static [[gnu::pure]] int gnu_pure(int x) {
    return x + 10;
}

static [[gnu::const]] int gnu_const(int x) {
    return x * 3;
}

// [from test_pure_dead_call_elim]
// Test dead-call elimination for pure/const functions.
// When a pure/const call's result is unused, the call is skipped at -O1+.
// Argument side effects must still execute.

static __attribute__((pure)) int _pure_dead_call_elim_square(int x) {
    return x * x;
}

static __attribute__((const)) int cube(int x) {
    return x * x * x;
}

#pragma cccc suite begin "optimizer"

// test_calln_callf_dead_elim
[[cccc::test(return = 42, flags = "-O1")]]
int test_calln_callf_dead_elim(void) {
    int n = 3;

    // CALLN via a const-annotated function pointer type: result unused.
    // Dead-call elimination should skip the call dispatch while ++n runs.
    const_fn_t fp_c = triple;
    fp_c(++n); // n becomes 4; call result unused
    if (n != 4)
        return 1;

    // CALLN via a pure-annotated function pointer type: result unused.
    pure_fn_t fp_p = double_it;
    fp_p(++n); // n becomes 5; call result unused
    if (n != 5)
        return 2;

    // Result used: call must still execute correctly.
    if (triple(3) != 9)
        return 3;
    if (double_it(4) != 8)
        return 4;

    // Multiple unused indirect calls: all side effects must run.
    int k = 0;
    fp_c(++k);
    fp_c(++k);
    if (k != 2)
        return 5;

    return 42;
}

// test_cse_const_calls
[[cccc::test(return = 42, flags = "-O2")]]
int test_cse_const_calls(void) {
    // --- constant-arg CSE ---
    int a = square(7);
    int b = square(7); // same constant arg -> CSE
    if (a != 49)
        return 1;
    if (b != 49)
        return 2;
    if (a != b)
        return 3;

    // --- local-variable CSE: p not modified between calls ---
    int p = 5;
    int c = square(p);
    int d = square(p); // same local slot, unchanged -> CSE
    if (c != 25)
        return 4;
    if (d != 25)
        return 5;
    if (c != d)
        return 6;

    // --- no CSE: p modified between calls ---
    int e = square(p); // p==5, result 25
    p     = 6;
    int f = square(p); // p==6, result 36
    if (e != 25)
        return 7;
    if (f != 36)
        return 8;
    if (e == f)
        return 9; // must differ

    // --- multi-arg CSE ---
    int u = add2(3, 4);
    int v = add2(3, 4); // same two constant args -> CSE
    if (u != 7)
        return 10;
    if (v != 7)
        return 11;
    if (u != v)
        return 12;

    return 42;
}

// test_inline_dead
[[cccc::test(return = 42)]]
int test_inline_dead(void) {
    return 42;
}

// test_inline_locals
[[cccc::test(return = 42, flags = "-O2")]]
int test_inline_locals(void) {
    if (sum_to(5) != 15)
        return 1;
    if (sum_to(10) != 55)
        return 2;
    if (fib(0) != 0)
        return 3;
    if (fib(1) != 1)
        return 4;
    if (fib(10) != 55)
        return 5;
    return 42;
}

// test_inline_multistmt
[[cccc::test(return = 42, flags = "-O2")]]
int test_inline_multistmt(void) {
    if (clamp(5, 10, 20) != 10)
        return 1;
    if (clamp(25, 10, 20) != 20)
        return 2;
    if (clamp(15, 10, 20) != 15)
        return 3;
    if (min(3, 7) != 3)
        return 4;
    if (max(3, 7) != 7)
        return 5;
    if (sign(42) != 1)
        return 6;
    if (sign(-5) != -1)
        return 7;
    if (sign(0) != 0)
        return 8;
    if (clamp(min(0, 50), 10, 30) != 10)
        return 9;
    if (clamp(max(0, 50), 10, 30) != 30)
        return 10;
    if (min(clamp(-5, 0, 10), 5) != 0)
        return 11;
    return 42;
}

// test_inline_singlereturn
[[cccc::test(return = 42)]]
int test_inline_singlereturn(void) {
    if (add(10, 20) != 30)
        return 1;
    if (_inline_singlereturn_max(15, 30) != 30)
        return 2;
    if (add(add(5, 3), 2) != 10)
        return 3;
    if (_inline_singlereturn_clamp(5, 10, 20) != 10)
        return 4;
    if (_inline_singlereturn_clamp(25, 10, 20) != 20)
        return 5;
    if (_inline_singlereturn_clamp(15, 10, 20) != 15)
        return 6;
    return 42;
}

// test_inline_threshold
[[cccc::test(return = 42, flags = "-O2")]]
int test_inline_threshold(void) {
    if (big_add(1, 2, 3, 4) != 10)
        return 1;
    if (big_add(10, 20, 30, 40) != 100)
        return 2;
    return 42;
}

// test_inline_void
[[cccc::test(return = 42, flags = "-O2")]]
int test_inline_void(void) {
    incr(10);
    incr(20);
    incr(30);
    if (counter != 60)
        return 1;
    return 42;
}

// test_optimizer_compaction
[[cccc::test(return = 42, flags = "--optimize=3")]]
int test_optimizer_compaction(void) {
    int folded = ((10 + 20) * (3 - 1)) ^ (7 & 3);
    if (folded != 63)
        return 1;

    if (global_fp(35) != 42)
        return 2;

    if (label_value(2) != 30)
        return 3;

    if (dense_switch(41) != 42)
        return 4;
    if (dense_switch(64) != -1)
        return 5;

    return 42;
}

// test_optimizer_constant_fold
[[cccc::test(return = 42)]]
int test_optimizer_constant_fold(void) {
    if ((21 + 21) != 42)
        return 4;
    if ((50 - 8) != 42)
        return 5;
    if ((6 * 7) != 42)
        return 6;
    if ((42 + 0) != 42)
        return 7;
    if ((42 * 1) != 42)
        return 8;

    int a = 40;
    int b = 2;
    int c = a + b;
    int d = choose(0);

    if (c != 42)
        return 1;
    if (d != 9)
        return 2;
    if ((7 / (d - 9 + 1)) != 7)
        return 3;
    return 42;
}

// test_optimizer_indexed_ops
[[cccc::test(return = 42, flags = "--optimize=3")]]
int test_optimizer_indexed_ops(void) {
    int    ints[8];
    char   bytes[8];
    double doubles[4];

    for (int i = 0; i < 8; i++) {
        ints[i]  = i * 3 + 1;
        bytes[i] = (char)(i + 2);
    }
    for (int i = 0; i < 4; i++)
        doubles[i] = (double)(i + 1) * 1.5;

    int sum = 0;
    for (int i = 0; i < 8; i++) {
        sum = sum + ints[i];
        sum = sum + bytes[i];
    }

    int scaled = 0;
    for (int i = 0; i < 4; i++)
        scaled = scaled + (int)(doubles[i] * 2.0);

    if (sum != 136)
        return 1;
    if (scaled != 30)
        return 2;
    return 42;
}

// test_optimizer_linear_statements
[[cccc::test(return = 42, flags = "--optimize=4")]]
int test_optimizer_linear_statements(void) {
    int a = 0;
    if ((0 + 1) == 1)
        a = a + 1;
    else
        a = a - 1;
    if ((1 + 1) == 2)
        a = a + 1;
    else
        a = a - 1;
    if ((2 + 1) == 3)
        a = a + 1;
    else
        a = a - 1;
    if ((3 + 1) == 4)
        a = a + 1;
    else
        a = a - 1;
    if ((4 + 1) == 5)
        a = a + 1;
    else
        a = a - 1;
    if ((5 + 1) == 6)
        a = a + 1;
    else
        a = a - 1;
    if ((6 + 1) == 7)
        a = a + 1;
    else
        a = a - 1;
    if ((7 + 1) == 8)
        a = a + 1;
    else
        a = a - 1;
    if ((8 + 1) == 9)
        a = a + 1;
    else
        a = a - 1;
    if ((9 + 1) == 10)
        a = a + 1;
    else
        a = a - 1;
    if ((10 + 1) == 11)
        a = a + 1;
    else
        a = a - 1;
    if ((11 + 1) == 12)
        a = a + 1;
    else
        a = a - 1;
    if ((12 + 1) == 13)
        a = a + 1;
    else
        a = a - 1;
    if ((13 + 1) == 14)
        a = a + 1;
    else
        a = a - 1;
    if ((14 + 1) == 15)
        a = a + 1;
    else
        a = a - 1;
    if ((15 + 1) == 16)
        a = a + 1;
    else
        a = a - 1;
    if ((16 + 1) == 17)
        a = a + 1;
    else
        a = a - 1;
    if ((17 + 1) == 18)
        a = a + 1;
    else
        a = a - 1;
    if ((18 + 1) == 19)
        a = a + 1;
    else
        a = a - 1;
    if ((19 + 1) == 20)
        a = a + 1;
    else
        a = a - 1;
    if ((20 + 1) == 21)
        a = a + 1;
    else
        a = a - 1;
    if ((21 + 1) == 22)
        a = a + 1;
    else
        a = a - 1;
    if ((22 + 1) == 23)
        a = a + 1;
    else
        a = a - 1;
    if ((23 + 1) == 24)
        a = a + 1;
    else
        a = a - 1;
    if ((24 + 1) == 25)
        a = a + 1;
    else
        a = a - 1;
    if ((25 + 1) == 26)
        a = a + 1;
    else
        a = a - 1;
    if ((26 + 1) == 27)
        a = a + 1;
    else
        a = a - 1;
    if ((27 + 1) == 28)
        a = a + 1;
    else
        a = a - 1;
    if ((28 + 1) == 29)
        a = a + 1;
    else
        a = a - 1;
    if ((29 + 1) == 30)
        a = a + 1;
    else
        a = a - 1;
    if ((30 + 1) == 31)
        a = a + 1;
    else
        a = a - 1;
    if ((31 + 1) == 32)
        a = a + 1;
    else
        a = a - 1;
    if ((32 + 1) == 33)
        a = a + 1;
    else
        a = a - 1;
    if ((33 + 1) == 34)
        a = a + 1;
    else
        a = a - 1;
    if ((34 + 1) == 35)
        a = a + 1;
    else
        a = a - 1;
    if ((35 + 1) == 36)
        a = a + 1;
    else
        a = a - 1;
    if ((36 + 1) == 37)
        a = a + 1;
    else
        a = a - 1;
    if ((37 + 1) == 38)
        a = a + 1;
    else
        a = a - 1;
    if ((38 + 1) == 39)
        a = a + 1;
    else
        a = a - 1;
    if ((39 + 1) == 40)
        a = a + 1;
    else
        a = a - 1;
    if ((40 + 1) == 41)
        a = a + 1;
    else
        a = a - 1;
    if ((41 + 1) == 42)
        a = a + 1;
    else
        a = a - 1;
    if ((42 + 1) == 43)
        a = a + 1;
    else
        a = a - 1;
    if ((43 + 1) == 44)
        a = a + 1;
    else
        a = a - 1;
    if ((44 + 1) == 45)
        a = a + 1;
    else
        a = a - 1;
    if ((45 + 1) == 46)
        a = a + 1;
    else
        a = a - 1;
    if ((46 + 1) == 47)
        a = a + 1;
    else
        a = a - 1;
    if ((47 + 1) == 48)
        a = a + 1;
    else
        a = a - 1;
    if ((48 + 1) == 49)
        a = a + 1;
    else
        a = a - 1;
    if ((49 + 1) == 50)
        a = a + 1;
    else
        a = a - 1;
    if ((50 + 1) == 51)
        a = a + 1;
    else
        a = a - 1;
    if ((51 + 1) == 52)
        a = a + 1;
    else
        a = a - 1;
    if ((52 + 1) == 53)
        a = a + 1;
    else
        a = a - 1;
    if ((53 + 1) == 54)
        a = a + 1;
    else
        a = a - 1;
    if ((54 + 1) == 55)
        a = a + 1;
    else
        a = a - 1;
    if ((55 + 1) == 56)
        a = a + 1;
    else
        a = a - 1;
    if ((56 + 1) == 57)
        a = a + 1;
    else
        a = a - 1;
    if ((57 + 1) == 58)
        a = a + 1;
    else
        a = a - 1;
    if ((58 + 1) == 59)
        a = a + 1;
    else
        a = a - 1;
    if ((59 + 1) == 60)
        a = a + 1;
    else
        a = a - 1;
    if (a != 60)
        return 1;
    return 42;
}

// test_optimizer_scalar_promotion
[[cccc::test(return = 42, flags = "--optimize=3")]]
int test_optimizer_scalar_promotion(void) {
    int sum = 0;
    int i   = 0;
    for (; i < 12; i++)
        sum += i;

    int mixed = 1;
    for (int j = 0; j < 4; j++) {
        mixed += bump(j);
        if (mixed & 1)
            mixed += j;
        else
            mixed -= j;
    }

    if (sum != 66)
        return 1;
    if (i != 12)
        return 2;
    if (mixed != 45)
        return 3;
    return 42;
}

// test_pure_const_attr
[[cccc::test(return = 42)]]
int test_pure_const_attr(void) {
    if (pure_fn(5) != 10)
        return 1;
    if (const_fn(5) != 6)
        return 2;
    if (static_pure(5) != 4)
        return 3;
    if (static_const(5) != 25)
        return 4;
    if (gnu_pure(5) != 15)
        return 5;
    if (gnu_const(5) != 15)
        return 6;
    return 42;
}

// test_pure_dead_call_elim
[[cccc::test(return = 42, flags = "-O1")]]
int test_pure_dead_call_elim(void) {
    int n = 3;

    // Result unused: call should be eliminated, but ++n must still run
    _pure_dead_call_elim_square(++n);
    if (n != 4)
        return 1;

    // Result unused with const function: ++n must still run
    cube(++n);
    if (n != 5)
        return 2;

    // Result used: call must still execute normally
    if (_pure_dead_call_elim_square(4) != 16)
        return 3;
    if (cube(3) != 27)
        return 4;

    // Multiple discarded calls in sequence
    int k = 0;
    _pure_dead_call_elim_square(++k);
    _pure_dead_call_elim_square(++k);
    if (k != 2)
        return 5;

    return 42;
}

// test_optimizer_fmadd_precision: FMADD3_FMA single-rounding path (--fma)
// Uses exact-representable values so FMA and two-rounding agree.
[[cccc::test(return = 42, flags = "--fma --optimize=4")]]
int test_optimizer_fmadd_precision(void) {
    static double dot_d(const double *a, const double *b, int n) {
        double sum = 0.0;
        for (int i = 0; i < n; i++)
            sum += a[i] * b[i];
        return sum;
    }
    static float dot_f(const float *a, const float *b, int n) {
        float sum = 0.0f;
        for (int i = 0; i < n; i++)
            sum += a[i] * b[i];
        return sum;
    }
    double a[3] = {1.0, 2.0, 4.0};
    double b[3] = {1.0, 2.0, 4.0};
    if (dot_d(a, b, 3) != 21.0)
        return 1;
    float fa[3] = {1.0f, 2.0f, 4.0f};
    float fb[3] = {1.0f, 2.0f, 4.0f};
    if (dot_f(fa, fb, 3) != 21.0f)
        return 2;
    return 42;
}

// test_optimizer_fmsub_precision: FMSUB3_FMA single-rounding path (--fma)
[[cccc::test(return = 42, flags = "--fma --optimize=4")]]
int test_optimizer_fmsub_precision(void) {
    static double dot_sub_d(const double *a, const double *b, int n) {
        double sum = 0.0;
        for (int i = 0; i < n; i++)
            sum -= a[i] * b[i];
        return sum;
    }
    static float dot_sub_f(const float *a, const float *b, int n) {
        float sum = 0.0f;
        for (int i = 0; i < n; i++)
            sum -= a[i] * b[i];
        return sum;
    }
    double a[3] = {1.0, 2.0, 4.0};
    double b[3] = {1.0, 2.0, 4.0};
    if (dot_sub_d(a, b, 3) != -21.0)
        return 1;
    float fa[3] = {1.0f, 2.0f, 4.0f};
    float fb[3] = {1.0f, 2.0f, 4.0f};
    if (dot_sub_f(fa, fb, 3) != -21.0f)
        return 2;
    return 42;
}

// test_optimizer_fnmsub_precision: FNMSUB3_FMA single-rounding path (--fma)
[[cccc::test(return = 42, flags = "--fma --optimize=4")]]
int test_optimizer_fnmsub_precision(void) {
    static double acc_sub_d(const double *a, const double *b, int n) {
        double sum = 1000.0;
        for (int i = 0; i < n; i++)
            sum -= a[i] * b[i];
        return sum;
    }
    static float acc_sub_f(const float *a, const float *b, int n) {
        float sum = 1000.0f;
        for (int i = 0; i < n; i++)
            sum -= a[i] * b[i];
        return sum;
    }
    double a[3] = {1.0, 2.0, 4.0};
    double b[3] = {1.0, 2.0, 4.0};
    if (acc_sub_d(a, b, 3) != 979.0)
        return 1;
    float fa[3] = {1.0f, 2.0f, 4.0f};
    float fb[3] = {1.0f, 2.0f, 4.0f};
    if (acc_sub_f(fa, fb, 3) != 979.0f)
        return 2;
    return 42;
}

// [from test_optimizer_elim_ext]
// Test redundant sign/zero-extension elimination (-felim-ext).
#include <stdint.h>

static int opt_elim_failures = 0;
#define OPT_EXPECT(expr, expected)                                             \
    do {                                                                       \
        long long got  = (long long)(expr);                                    \
        long long want = (long long)(expected);                                \
        if (got != want)                                                       \
            opt_elim_failures++;                                               \
    } while (0)

static long long opt_non_adjacent_sx4(int *p) {
    int       v = *p;
    long long a = v;
    long long b = a + 1;
    long long c = (int)b;
    return c;
}
static long long opt_chained_sx4(long long x) {
    int       a = (int)x;
    int       b = a;
    long long c = (int)b;
    return c;
}
static long long opt_zx1_then_zx2(unsigned char v) {
    unsigned short s = v;
    return (long long)s;
}
static long long opt_zx4_after_sx4(int v) {
    unsigned int u = (unsigned int)v;
    return (long long)u;
}
static long long opt_branch_reset(int cond, int a, int b) {
    int v;
    if (cond)
        v = a;
    else
        v = b;
    long long r = (int)v;
    return r;
}
static long long opt_local_zx4_chain(unsigned int u) {
    unsigned int       a = u;
    unsigned long long b = a;
    return (long long)b;
}
static long long opt_sx4_on_zx2(unsigned short h) {
    int i = (int)h;
    return (long long)i;
}

[[cccc::test(return = 42, flags = "-felim-ext")]]
int test_optimizer_elim_ext(void) {
    int base = 7;
    OPT_EXPECT(opt_non_adjacent_sx4(&base), 8);
    OPT_EXPECT(opt_chained_sx4(-5LL), -5);
    OPT_EXPECT(opt_chained_sx4(2147483647LL), 2147483647);
    OPT_EXPECT(opt_zx1_then_zx2(200), 200);
    OPT_EXPECT(opt_zx4_after_sx4(-1), (long long)(unsigned int)-1);
    OPT_EXPECT(opt_branch_reset(1, 42, 0), 42);
    OPT_EXPECT(opt_branch_reset(0, 0, -99), -99);
    OPT_EXPECT(opt_local_zx4_chain(0u), 0);
    OPT_EXPECT(opt_local_zx4_chain(4294967295u), 4294967295LL);
    OPT_EXPECT(opt_sx4_on_zx2(65535), 65535);
    if (opt_elim_failures)
        return 1;
    return 42;
}

// [from test_optimizer_fuse_ops]
// Verify fused-ops (-ffuse) on a simple arithmetic chain.
[[cccc::test(return = 42, flags = "-ffuse")]]
int test_optimizer_fuse_ops(void) {
    long x = 7;
    long y = x * 6;
    long z = y + 0;
    return (int)z;
}

// [from test_optimizer_o4_fused_arith]
// Verify --optimize=4 on a base+index*6 expression.
static long opt_o4_seed = 5;
[[cccc::test(return = 42, flags = "--optimize=4")]]
int test_optimizer_o4_fused_arith(void) {
    long base   = 12;
    long index  = opt_o4_seed;
    long result = base + index * 6;
    return (int)result;
}

// [from test_optimizer_tail_call]
// Tail-call optimisation: direct recursion and mutual recursion with large
// depths.
#include <stdio.h>

static long opt_tail_sum(long n, long acc) {
    if (n <= 0)
        return acc;
    return opt_tail_sum(n - 1, acc + n);
}
static int opt_is_odd(int n);
static int opt_is_even(int n) {
    if (n == 0)
        return 1;
    return opt_is_odd(n - 1);
}
static int opt_is_odd(int n) {
    if (n == 0)
        return 0;
    return opt_is_even(n - 1);
}
static int opt_one(void) {
    return 1;
}
static int opt_non_tail(int n) {
    if (n <= 0)
        return 0;
    return opt_non_tail(n - 1) + 1;
}
static int opt_double_it(int x) {
    return x * 2;
}
static int opt_add_one(int x) {
    return x + 1;
}
static int opt_compose(int x) {
    return opt_add_one(opt_double_it(x));
}

[[cccc::test(return = 42, flags = "-O1")]]
int test_optimizer_tail_call(void) {
    (void)opt_one;
    long s = opt_tail_sum(500000, 0);
    if (s != 125000250000L)
        return 1;
    if (!opt_is_even(200000))
        return 2;
    if (opt_is_even(200001))
        return 3;
    if (opt_non_tail(200) != 200)
        return 4;
    if (opt_compose(5) != 11)
        return 5;
    return 42;
}

// [from test_atomic_ops_o3]
// Atomic exchange/CAS at -O3 (regression guard for copy-prop width_enc aliasing
// #497).
#include <stdatomic.h>

[[cccc::test(return = 42, flags = "-O3")]]
int test_atomic_ops_o3(void) {
    atomic_int xi = 0;
    int a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, g = 7, h = 8, i = 9, j = 10,
        k = 11, l = 12;
    atomic_store(&xi, 100);
    int old_int = atomic_exchange(&xi, 200);
    if (old_int != 100)
        return 1;
    if (atomic_load(&xi) != 200)
        return 2;
    int sum = a + b + c + d + e + f + g + h + i + j + k + l;
    if (sum != 78)
        return 3;
    int expected_i = 200;
    int r          = atomic_compare_exchange_strong(&xi, &expected_i, 300);
    if (!r || atomic_load(&xi) != 300)
        return 4;
    expected_i = 999;
    r          = atomic_compare_exchange_strong(&xi, &expected_i, 400);
    if (r || atomic_load(&xi) != 300 || expected_i != 300)
        return 5;

    atomic_long xl = 0;
    long        p = 100, q = 200, s2 = 300, t = 400, u = 500, v = 600, w2 = 700,
                x2 = 800, y = 900, z = 1000;
    atomic_store(&xl, 10000LL);
    long old_long = atomic_exchange(&xl, 20000LL);
    if (old_long != 10000LL)
        return 6;
    if (atomic_load(&xl) != 20000LL)
        return 7;
    long lsum = p + q + s2 + t + u + v + w2 + x2 + y + z;
    if (lsum != 5500LL)
        return 8;
    long expected_l = 20000LL;
    int  rl         = atomic_compare_exchange_strong(&xl, &expected_l, 30000LL);
    if (!rl || atomic_load(&xl) != 30000LL)
        return 9;
    expected_l = 99999LL;
    rl         = atomic_compare_exchange_strong(&xl, &expected_l, 40000LL);
    if (rl || atomic_load(&xl) != 30000LL || expected_l != 30000LL)
        return 10;
    return 42;
}

#pragma cccc suite end
