// Tests for per-function optimization via the optimize attribute.
// CCCC_FLAGS: --testing
//
// Three surfaces are tested:
//   1. __attribute__((optimize("O2")))  — GCC-compatible string form
//   2. [[cccc::optimize(2)]]            — C23 cccc-native integer form
//   3. @optimize(2)                     — @ shorthand (rewrites to [[cccc::optimize]])
//
// Precedence: GCC-style — the attribute WINS over the global -O flag.
// Functions with an optimize attribute use their own level regardless of CLI
// -O or #pragma cccc config(optimisation=N).  Functions without an attribute
// use the global level as normal.
//
// All tests below are semantics-preserving: the attribute only changes how the
// bytecode is optimized, not what the function computes.  Results are
// identical at every global opt level — safe under `tools/tests.py --full`.

// ---- GNU-style string form ----

__attribute__((optimize("O2")))
static int gnu_attr_add(int a, int b) {
    return a + b;
}

[[cccc::test]]
static void test_gnu_str_attr(void) {
    $assert_eq(gnu_attr_add(3, 4), 7);
    $assert_eq(gnu_attr_add(0, 0), 0);
    $assert_eq(gnu_attr_add(-1, 1), 0);
}

// ---- C23 [[cccc::optimize(N)]] integer form ----

[[cccc::optimize(3)]]
static long c23_attr_mul(long a, long b) {
    return a * b;
}

[[cccc::test]]
static void test_c23_int_attr(void) {
    $assert_eq(c23_attr_mul(6, 7), 42);
    $assert_eq(c23_attr_mul(0, 100), 0);
    $assert_eq(c23_attr_mul(-3, -3), 9);
}

// ---- @ shorthand ----

@optimize(1)
static int at_attr_sub(int a, int b) {
    return a - b;
}

[[cccc::test]]
static void test_at_attr(void) {
    $assert_eq(at_attr_sub(10, 3), 7);
    $assert_eq(at_attr_sub(0, 0), 0);
}

// ---- O0 attribute: explicit disable ----
// Useful to mark a function as intentionally unoptimized (e.g. for timing
// stability or debugging).

[[cccc::optimize(0)]]
static int o0_attr_fn(int x) {
    return x * 2;
}

[[cccc::test]]
static void test_o0_attr(void) {
    $assert_eq(o0_attr_fn(21), 42);
    $assert_eq(o0_attr_fn(0), 0);
}

// ---- C23 string form: [[cccc::optimize("O2")]] ----

[[cccc::optimize("O2")]]
static int c23_str_attr_fn(int x) {
    return x + 1;
}

[[cccc::test]]
static void test_c23_str_attr(void) {
    $assert_eq(c23_str_attr_fn(41), 42);
    $assert_eq(c23_str_attr_fn(-1), 0);
}

// ---- GCC-compatible dash prefix: "-O2" ----

__attribute__((optimize("-O3")))
static int gcc_dash_attr(int a, int b) {
    return a - b;
}

[[cccc::test]]
static void test_gcc_dash_attr(void) {
    $assert_eq(gcc_dash_attr(10, 4), 6);
    $assert_eq(gcc_dash_attr(5, 5), 0);
}

// ---- Mixing attributed and non-attributed functions in the same TU ----
// Ensures the per-function path does not corrupt non-attributed function code.

static int plain_sq(int x) {
    return x * x;
}

[[cccc::optimize(2)]]
static int opt_double(int x) {
    return x + x;
}

[[cccc::test]]
static void test_mixed(void) {
    $assert_eq(plain_sq(5), 25);
    $assert_eq(plain_sq(0), 0);
    $assert_eq(opt_double(6), 12);
    $assert_eq(opt_double(-3), -6);
}

// ---- O4/fuse-ops level ----

[[cccc::optimize(4)]]
static long opt4_muladd(long a, long b, long c) {
    return a * b + c;
}

[[cccc::test]]
static void test_o4_attr(void) {
    $assert_eq(opt4_muladd(3, 4, 5), 17);
    $assert_eq(opt4_muladd(0, 100, 7), 7);
}
