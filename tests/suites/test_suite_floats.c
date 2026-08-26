// CCCC_FLAGS: --testing
// Consolidated suite: floating-point, complex, FMA/FMS optimizer, float32
// Source tests: test_complex, test_complex_tgmath, test_fenv_tgmath_hexfloat,
// test_float, test_float32_register_file, test_float32_width, test_float_mixed,
// test_float_simple, test_fstr_promoted, test_optimizer_fmadd,
// test_optimizer_fmsub, test_optimizer_fnmsub, test_optimizer_fp_promotion,
//   test_float_comprehensive, test_fp_minimal

#include <complex.h>
#include <tgmath.h>
#include <fenv.h>
#include <math.h>
#include <stdlib.h>

// [from test_complex]
struct Box {
    double complex z;
};

// [from test_float]
// Test floating-point arithmetic operations
// Expected return: 42

// [from test_float32_register_file]
// Test native f32 register arithmetic and raw-bit save/restore

static float add_half(float x) {
    return x + 0.5f;
}

static float call_add_half(float x) {
    return add_half(x);
}

static float sum3(float a, float b, float c) {
    return a + b + c;
}

static int check_args(float a, float b, float c) {
    float x = sum3(a, call_add_half(b), c);
    return x == 42.0f;
}

// [from test_float32_width]
// Test float uses 32-bit load/store and rounding semantics

float gf    = 1.25f;
float ga[3] = {1.0f, 2.0f, 39.0f};

struct S {
    char  c;
    float f;
    int   i;
};

static float add_one(float x) {
    return x + 1.0f;
}

static int check_param(float x, float y) {
    float z = x + y;
    return z == 42.0f;
}

// [from test_float_mixed]
// Test mixed int and float function parameters
// Expected return: 42

static double add_int_float(int i, double d) {
    return i + d;
}

static double mixed_ops(double x, int n, double y) {
    return x * n + y;
}

static int double_to_int(double d) {
    // Implicit conversion
    return d;
}

// [from test_float_simple]
// Simple floating-point test
// Tests basic float arithmetic and comparisons
// Expected return: 42

// [from test_fstr_promoted]
// Regression test: float local promoted to FREG_S* then stored through a
// pointer (FSTR) or indexed (FSTR_INDEX). Sub-pass B must call MARK_FLOAT_USE
// for the float-source register in byte 0, and sub-pass C must count it.
// Without the fix, KILL_FLOAT_DEF incorrectly NOP'd the FMOV3 before FSTR.

static double store_via_ptr(double *out, double a, double b) {
    double x = 0.0;
    for (int i = 0; i < 4; i++)
        x += a;
    x    = b;   // Promoted register updated to b; previous loop value differs.
    *out = x;   // FMOV3 tmp=FREG_S0; FSTR tmp, *out — tmp must NOT be NOP'd.
    return 0.0; // Do not return x; only consumer of the promoted-read is FSTR.
}

static float store_via_ptr_f32(float *out, float a, float b) {
    float x = 0.0f;
    for (int i = 0; i < 4; i++)
        x += a;
    x    = b;
    *out = x;
    return 0.0f;
}

static double store_via_index(double *arr, double a, double b, int idx) {
    double x = 0.0;
    for (int i = 0; i < 4; i++)
        x += a;
    x        = b;
    arr[idx] = x; // indexed store: FSTR_INDEX
    return 0.0;
}

// [from test_optimizer_fmadd]
// multiply-accumulate loop — exercises FMUL3+FADD3 -> FMADD3 fusion

static double dot_d(const double *a, const double *b, int n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++)
        sum += a[i] * b[i];
    return sum;
}

// float variant — exercises FMUL3_F32+FADD3_F32 -> FMADD3_F32 fusion

static float dot_f(const float *a, const float *b, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++)
        sum += a[i] * b[i];
    return sum;
}

// mandelbrot-style inner loop: z = z*z + c

static int mandel_iters(double cr, double ci, int max) {
    double zr = 0.0, zi = 0.0;
    for (int i = 0; i < max; i++) {
        double zr2 = zr * zr - zi * zi + cr;
        double zi2 =
            zr * zi + zr * zi + ci; // 2*zr*zi+ci (two FMUL+FADD chains)
        zr = zr2;
        zi = zi2;
        if (zr2 * zr2 + zi2 * zi2 > 4.0)
            return i;
    }
    return max;
}

// [from test_optimizer_fmsub]
// Verifies correctness of FMSUB3 semantics (two-rounding: product rounded
// first, then subtracted). The fusion pass emits FMSUB3 for the minuend form
// (a*b - c) when FMUL3 and FSUB3 are adjacent with no intervening instructions.
// Patterns where a load separates the pair fall back to unfused FMUL3+FSUB3
// (same numerical result).

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

// mandelbrot-style: exercises FP subtraction-of-products pattern

static int mandel_sub_iters(double cr, double ci, int max) {
    double zr = 0.0, zi = 0.0;
    for (int i = 0; i < max; i++) {
        double zr2 = zr * zr - zi * zi + cr;
        double zi2 = 2.0 * zr * zi + ci;
        zr         = zr2;
        zi         = zi2;
        if (zr * zr + zi * zi > 4.0)
            return i;
    }
    return max;
}

// [from test_optimizer_fnmsub]
// Verifies correctness of FNMSUB3 semantics (two-rounding: product rounded
// first, then subtracted from base).  The fusion pass emits FNMSUB3 for the
// accumulating-subtract form (base - a*b) when FMUL3 and FSUB3 are adjacent
// with the multiply result in the subtrahend (RS2) position.
// Dead-FMOV3 elimination in copy-prop allows fusion to fire even when float
// local promotion inserts a FMOV3 between FMUL3 and FSUB3.

// Accumulating subtract: sum -= a[i]*b[i]  →  sum = sum - a[i]*b[i]

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

// Verify result matches manually computed unfused sequence (two-rounding).

static double acc_sub_ref(const double *a, const double *b, int n) {
    double sum = 1000.0;
    for (int i = 0; i < n; i++) {
        double prod = a[i] * b[i];
        sum         = sum - prod;
    }
    return sum;
}

// Pattern: diff -= scale * value  (common in numerical methods)

static double weighted_diff(double init, double scale, double v1, double v2) {
    double result  = init;
    result        -= scale * v1;
    result        -= scale * v2;
    return result;
}

// mandelbrot inner loop exercises both FMADD3 and FNMSUB3:
//   zr_next = zr*zr - zi*zi + cr  → FMSUB3 (minuend form: zr*zr - zi*zi)
//   zi_next = 2*zr*zi + ci        → FMADD3
//   escape check: zr*zr + zi*zi   → FMADD3

static int _optimizer_fnmsub_mandel_iters(double cr, double ci, int max) {
    double zr = 0.0, zi = 0.0;
    for (int i = 0; i < max; i++) {
        double zr2 = zr * zr - zi * zi + cr;
        double zi2 = 2.0 * zr * zi + ci;
        zr         = zr2;
        zi         = zi2;
        if (zr * zr + zi * zi > 4.0)
            return i;
    }
    return max;
}

// [from test_optimizer_fp_promotion]
// double accumulator loop (mandelbrot-style)

static double sum_double(int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++)
        s += (double)i * 0.5;
    return s;
}

// float accumulator loop

static float sum_float(int n) {
    float s = 0.0f;
    for (int i = 0; i < n; i++)
        s += (float)i * 0.5f;
    return s;
}

// double param: not initialised locally — promoted from stack slot

static double scale(double x, int n) {
    for (int i = 0; i < n; i++)
        x *= 2.0;
    return x;
}

// multiple double locals competing for slots

static double multi(int n) {
    double a = 1.0, b = 2.0, c = 3.0;
    for (int i = 0; i < n; i++) {
        a += b;
        b += c;
        c += a;
    }
    return a + b + c;
}

#pragma cccc suite begin "floats"

// test_complex
[[cccc::test(return = 42)]]
int test_complex(void) {
    double _Complex z = CMPLX(1.0, 2.0);
    if (sizeof(z) != 16)
        return 1;
    if (_Alignof(double _Complex) != 8)
        return 2;
    if (creal(z) != 1.0)
        return 3;
    if (cimag(z) != 2.0)
        return 4;

    double complex w = z * CMPLX(3.0, 4.0);
    if (creal(w) != -5.0)
        return 5;
    if (cimag(w) != 10.0)
        return 6;

    w = w / CMPLX(3.0, 4.0);
    if (creal(w) != 1.0)
        return 7;
    if (cimag(w) != 2.0)
        return 8;

    w = z + CMPLX(5.0, 6.0) - CMPLX(1.0, 1.0);
    if (creal(w) != 5.0)
        return 9;
    if (cimag(w) != 7.0)
        return 10;

    w = -z;
    if (creal(w) != -1.0)
        return 11;
    if (cimag(w) != -2.0)
        return 12;

    w = conj(z);
    if (creal(w) != 1.0)
        return 13;
    if (cimag(w) != -2.0)
        return 14;

    w = 5.0;
    if (creal(w) != 5.0)
        return 15;
    if (cimag(w) != 0.0)
        return 16;

    struct Box box;
    box.z = CMPLX(8.0, 9.0);
    if (creal(box.z) != 8.0)
        return 17;
    if (cimag(box.z) != 9.0)
        return 18;

    float complex f = CMPLXF(1.0f, 2.0f);
    if (sizeof(f) != 8)
        return 19;
    if (crealf(f) != 1.0f)
        return 20;
    if (cimagf(f) != 2.0f)
        return 21;

    int selected = _Generic(z, double complex: 42, default: 0);
    if (selected != 42)
        return 22;

    if (z != CMPLX(1.0, 2.0))
        return 23;
    if (z == CMPLX(2.0, 1.0))
        return 24;

    return 42;
}

// test_complex_tgmath
[[cccc::test(return = 42)]]
int test_complex_tgmath(void) {
    double complex z = CMPLX(3.0, 4.0);
    if (fabs(z) != 5.0)
        return 1;
    if (cabs(z) != 5.0)
        return 2;
    if (carg(CMPLX(1.0, 0.0)) != 0.0)
        return 3;

    double complex i = I;
    if (creal(i) != 0.0)
        return 4;
    if (cimag(i) != 1.0)
        return 5;

    double _Imaginary compat = I;
    if (creal(compat) != 0.0)
        return 6;
    if (cimag(compat) != 1.0)
        return 7;

    return 42;
}

// [helper for test_complex_nesting]
static double complex_nesting_helper(void) {
    return 7.0;
}

// test_complex_nesting (#968)
//
// gen_complex_expr()'s ND_ADD/SUB/MUL/DIV case used to generate the RHS
// operand into fixed registers (T5/T6 + T7-T9 scratch). Since the function
// recurses, a right-hand-nested complex binop re-entered the same case and
// immediately reused those same registers for its own operands, destroying
// the outer RHS -- `20.0 + 22.0 * I` (parsed as `20.0 + (22.0 * I)`) is the
// canonical example. Left nesting survived by accident. A function call
// anywhere in the RHS was a second, related clobber: every temp register is
// caller-saved, so a call destroys the LHS regardless of which register it
// occupies. Both are covered here with real values (not just "compiles"),
// per the #963 lesson that a shape assertion alone cannot see a silent
// wrong-answer miscompile.
[[cccc::test(return = 42)]]
int test_complex_nesting(void) {
    // The literal repro from the ticket.
    double complex z = 20.0 + 22.0 * I;
    if (creal(z) != 20.0)
        return 1;
    if (cimag(z) != 22.0)
        return 2;

    double complex a = CMPLX(1.0, 2.0);
    double complex b = CMPLX(3.0, 4.0);
    double complex c = CMPLX(5.0, 6.0);

    // Right nesting vs. left nesting, all four operators.
    double complex add_r = a + (b + c), add_l = (a + b) + c;
    if (creal(add_r) != 9.0 || cimag(add_r) != 12.0)
        return 3;
    if (creal(add_l) != 9.0 || cimag(add_l) != 12.0)
        return 4;

    double complex sub_r = a - (b - c), sub_l = (a - b) - c;
    if (creal(sub_r) != 3.0 || cimag(sub_r) != 4.0)
        return 5;
    if (creal(sub_l) != -7.0 || cimag(sub_l) != -8.0)
        return 6;

    double complex mul_r = a * (b * c), mul_l = (a * b) * c;
    if (creal(mul_r) != -85.0 || cimag(mul_r) != 20.0)
        return 7;
    if (creal(mul_l) != -85.0 || cimag(mul_l) != 20.0)
        return 8;

    double complex div_r = a / (b / c), div_l = (a / b) / c;
    if (fabs(creal(div_r) - 1.72) > 1e-9)
        return 9;
    if (fabs(cimag(div_r) - 3.04) > 1e-9)
        return 10;
    if (fabs(creal(div_l) - 0.0439344262295082) > 1e-9)
        return 11;
    if (fabs(cimag(div_l) - (-0.03672131147540984)) > 1e-9)
        return 12;

    // Deep right-nested chain -- must not exhaust the temp register pool.
    double complex d0 = CMPLX(1.0, 1.0), d1 = CMPLX(2.0, 2.0);
    double complex d2 = CMPLX(3.0, 3.0), d3 = CMPLX(4.0, 4.0);
    double complex deep = d0 + (d1 + (d2 + (d3 + (d0 + (d1 + (d2 + d3))))));
    if (creal(deep) != 20.0 || cimag(deep) != 20.0)
        return 13;

    // float complex, right-nested.
    float complex fz = 2.0f + 3.0f * I;
    if (crealf(fz) != 2.0f)
        return 14;
    if (cimagf(fz) != 3.0f)
        return 15;

    // A function call in the RHS clobbers the LHS if it isn't spilled.
    double complex s = a + complex_nesting_helper();
    if (creal(s) != 8.0 || cimag(s) != 2.0)
        return 16;

    double complex m = a * (2.0 + complex_nesting_helper());
    if (creal(m) != 9.0 || cimag(m) != 18.0)
        return 17;

    return 42;
}

// test_fenv_tgmath_hexfloat
[[cccc::test(return = 42)]]
int test_fenv_tgmath_hexfloat(void) {
    double x = 0x1.8p+1;
    if (x != 3.0)
        return 1;

    if (feclearexcept(FE_ALL_EXCEPT) != 0)
        return 2;
    if (fetestexcept(FE_ALL_EXCEPT) != 0)
        return 3;

    float  f = -3.0f;
    double d = -4.0;
    if (fabs(f) != 3.0f)
        return 4;
    if (sqrt(d * d) != 4.0)
        return 5;

    return 42;
}

// test_float
[[cccc::test(return = 42)]]
int test_float(void) {
    // Test 1: Basic float literals and addition
    double x   = 10.5;
    double y   = 31.5;
    double sum = x + y; // 42.0

    // Test 2: Subtraction
    double a    = 50.0;
    double b    = 8.0;
    double diff = a - b; // 42.0

    // Test 3: Multiplication
    double m       = 6.0;
    double n       = 7.0;
    double product = m * n; // 42.0

    // Test 4: Division
    double p        = 84.0;
    double q        = 2.0;
    double quotient = p / q; // 42.0

    // Test 5: Unary minus
    double neg = -42.0;
    double pos = -neg; // 42.0

    // Test 6: Comparison operators
    double c1      = 42.0;
    double c2      = 42.0;
    double c3      = 41.0;

    int    eq_test = (c1 == c2); // 1
    int    ne_test = (c1 != c3); // 1
    int    lt_test = (c3 < c1);  // 1
    int    le_test = (c3 <= c1); // 1
    int    gt_test = (c1 > c3);  // 1
    int    ge_test = (c1 >= c2); // 1

    // Test 7: Type conversion int to float (implicit)
    int    int_val   = 21;
    double converted = int_val * 2.0; // 42.0 (int_val implicitly converted)

    // Test 8: Complex expression
    double complex_expr = 10.0 * 4.0 + 2.0; // 42.0

    // Test 9: Mixed expressions
    double mixed = 10.0 + 32.0; // 42.0

    // Test 10: Float variable assignment
    double result = 0.0;
    result        = 42.0;

    // Verify all tests pass and return 42
    if (sum != 42.0)
        return 1;
    if (diff != 42.0)
        return 2;
    if (product != 42.0)
        return 3;
    if (quotient != 42.0)
        return 4;
    if (pos != 42.0)
        return 5;

    if (!eq_test)
        return 6;
    if (!ne_test)
        return 7;
    if (!lt_test)
        return 8;
    if (!le_test)
        return 9;
    if (!gt_test)
        return 10;
    if (!ge_test)
        return 11;

    if (converted != 42.0)
        return 12;
    if (complex_expr != 42.0)
        return 13;
    if (mixed != 42.0)
        return 14;
    if (result != 42.0)
        return 15;

    return 42;
}

// test_float32_register_file
[[cccc::test(return = 42)]]
int test_float32_register_file(void) {
    float lhs      = 16.0f;
    float combined = lhs + call_add_half(25.5f);
    if (combined != 42.0f)
        return 1;

    if (!check_args(10.0f, 20.5f, 11.0f))
        return 2;

    float rounded = 16777216.0f + 1.0f;
    if (rounded != 16777216.0f)
        return 3;

    float neg = -rounded;
    if (neg >= 0.0f)
        return 4;

    return 42;
}

// test_float32_width
[[cccc::test(return = 42)]]
int test_float32_width(void) {
    struct S s;
    s.c           = 7;
    s.f           = 40.75f;
    s.i           = 9;

    float local   = 16777216.0f;
    float rounded = local + 1.0f;
    if (rounded != 16777216.0f)
        return 1;

    float casted = (float)16777217.0;
    if (casted != 16777216.0f)
        return 2;

    gf = gf + 0.75f;
    if (gf != 2.0f)
        return 3;

    ga[1] = ga[0] + ga[2];
    if (ga[1] != 40.0f)
        return 4;

    if (s.c != 7 || s.f != 40.75f || s.i != 9)
        return 5;

    if (add_one(s.f) != 41.75f)
        return 6;

    if (!check_param(20.5f, 21.5f))
        return 7;

    return 42;
}

// test_float_mixed
[[cccc::test(return = 42)]]
int test_float_mixed(void) {
    // Test 1: Int + Float parameters
    double result1 = add_int_float(10, 32.0);
    if (result1 != 42.0)
        return 1;

    // Test 2: Mixed parameter order
    double result2 = mixed_ops(20.0, 2, 2.0); // 20*2 + 2 = 42
    if (result2 != 42.0)
        return 2;

    // Test 3: Float to int conversion via function
    int result3 = double_to_int(42.0);
    if (result3 != 42)
        return 3;

    // Test 4: Chained calls
    double result4 =
        add_int_float(20, add_int_float(10, 12.0)); // 20 + (10 + 12)
    if (result4 != 42.0)
        return 4;

    return 42;
}

// test_float_simple
[[cccc::test(return = 42)]]
int test_float_simple(void) {
    // Basic arithmetic
    double a   = 20.0;
    double b   = 22.0;
    double sum = a + b; // 42.0

    // Test comparison
    if (sum == 42.0) {
        return 42;
    }

    return 0;
}

// test_fstr_promoted
[[cccc::test(return = 42, flags = "--optimize=3")]]
int test_fstr_promoted(void) {
    double d_out;
    store_via_ptr(&d_out, 1.0, 99.0);
    if (d_out != 99.0)
        return 1;

    float f_out;
    store_via_ptr_f32(&f_out, 1.0f, 88.0f);
    if (f_out != 88.0f)
        return 2;

    double arr[4] = {0};
    store_via_index(arr, 1.0, 77.0, 2);
    if (arr[2] != 77.0)
        return 3;

    return 42;
}

// test_optimizer_fmadd
[[cccc::test(return = 42, flags = "--optimize=4")]]
int test_optimizer_fmadd(void) {
    double a[4] = {1.0, 2.0, 3.0, 4.0};
    double b[4] = {5.0, 6.0, 7.0, 8.0};
    // 1*5 + 2*6 + 3*7 + 4*8 = 5+12+21+32 = 70
    if (dot_d(a, b, 4) != 70.0)
        return 1;

    // result must match manually computed value (two-rounding correctness)
    double manual = (1.0 * 5.0) + (2.0 * 6.0) + (3.0 * 7.0) + (4.0 * 8.0);
    if (dot_d(a, b, 4) != manual)
        return 2;

    float fa[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float fb[4] = {5.0f, 6.0f, 7.0f, 8.0f};
    if (dot_f(fa, fb, 4) != 70.0f)
        return 3;

    // mandelbrot: (0,0) is in the set — should return max
    if (mandel_iters(0.0, 0.0, 100) != 100)
        return 4;

    // (2,2) escapes quickly
    if (mandel_iters(2.0, 2.0, 100) != 0)
        return 5;

    return 42;
}

// test_optimizer_fmsub
[[cccc::test(return = 42, flags = "--optimize=4")]]
int test_optimizer_fmsub(void) {
    double a[4] = {1.0, 2.0, 3.0, 4.0};
    double b[4] = {5.0, 6.0, 7.0, 8.0};
    // -(1*5 + 2*6 + 3*7 + 4*8) = -(5+12+21+32) = -70
    if (dot_sub_d(a, b, 4) != -70.0)
        return 1;

    // result must match manually computed value (two-rounding correctness)
    double manual = -((1.0 * 5.0) + (2.0 * 6.0) + (3.0 * 7.0) + (4.0 * 8.0));
    if (dot_sub_d(a, b, 4) != manual)
        return 2;

    float fa[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float fb[4] = {5.0f, 6.0f, 7.0f, 8.0f};
    if (dot_sub_f(fa, fb, 4) != -70.0f)
        return 3;

    // mandelbrot: (0,0) is in the set — should return max
    if (mandel_sub_iters(0.0, 0.0, 100) != 100)
        return 4;

    // (2,2) escapes quickly
    if (mandel_sub_iters(2.0, 2.0, 100) != 0)
        return 5;

    return 42;
}

// test_optimizer_fnmsub
[[cccc::test(return = 42, flags = "--optimize=4")]]
int test_optimizer_fnmsub(void) {
    double a[4] = {1.0, 2.0, 3.0, 4.0};
    double b[4] = {5.0, 6.0, 7.0, 8.0};
    // 1000 - (1*5 + 2*6 + 3*7 + 4*8) = 1000 - 70 = 930
    if (acc_sub_d(a, b, 4) != 930.0)
        return 1;

    // Must match two-rounding reference exactly
    if (acc_sub_d(a, b, 4) != acc_sub_ref(a, b, 4))
        return 2;

    float fa[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float fb[4] = {5.0f, 6.0f, 7.0f, 8.0f};
    if (acc_sub_f(fa, fb, 4) != 930.0f)
        return 3;

    // weighted_diff: 100 - (0.5*8) - (0.5*12) = 100 - 4 - 6 = 90
    if (weighted_diff(100.0, 0.5, 8.0, 12.0) != 90.0)
        return 4;

    // mandelbrot: (0,0) is in the set — max iterations
    if (_optimizer_fnmsub_mandel_iters(0.0, 0.0, 100) != 100)
        return 5;

    // (2,2) escapes on first iteration
    if (_optimizer_fnmsub_mandel_iters(2.0, 2.0, 100) != 0)
        return 6;

    // (-2,0) is on the boundary — reaches max
    if (_optimizer_fnmsub_mandel_iters(-2.0, 0.0, 200) != 200)
        return 7;

    return 42;
}

// test_optimizer_fp_promotion
[[cccc::test(return = 42, flags = "--optimize=3")]]
int test_optimizer_fp_promotion(void) {
    // sum_double(100) == 0+0.5+1+...+49.5 == 2475.0
    if (sum_double(100) != 2475.0)
        return 1;

    // sum_float result should match within float precision
    float sf = sum_float(100);
    if (sf < 2474.0f || sf > 2476.0f)
        return 2;

    // scale(1.0, 10) == 2^10 == 1024.0
    if (scale(1.0, 10) != 1024.0)
        return 3;

    // multi: a,b,c start 1,2,3; after 5 iters check it's deterministic
    double m0 = multi(5);
    double m1 = multi(5);
    if (m0 != m1)
        return 4;
    if (m0 <= 0.0)
        return 5;

    return 42;
}

// Ticket #916: a discarded-value load through a float/double pointer used to
// segfault the VM (gen_expr's ND_DEREF/ND_MEMBER reused dest_reg==REG_ZERO as
// scratch for the address, so the load read from address 0; FLDR/FLDR_F32
// have no rd==REG_ZERO guard the way the integer LDR_* ops do). Each check
// below discards the load result but still requires the *side effect* of
// evaluating a live pointer/index to not crash -- if a discarded load reads
// the wrong address, one of the earlier discards would already have faulted.
struct discard_deref_s {
    float f;
};

[[cccc::test(return = 42)]]
int test_discarded_float_double_deref(void) {
    float  fx = 5.0f;
    float *pf = &fx;
    *pf;       // bare discarded float deref (the ticket's original repro)
    (void)*pf; // explicit (void)-discard

    double  dx = 5.0;
    double *pd = &dx;
    *pd;
    (void)*pd;

    long double  lx = 5.0L;
    long double *pl = &lx;
    *pl;

    struct discard_deref_s  s  = {1.0f};
    struct discard_deref_s *ps = &s;
    ps->f; // discarded member load through a pointer
    s.f;   // discarded member load, no pointer indirection

    float arr[3] = {1.0f, 2.0f, 3.0f};
    int   idx    = 1;
    arr[idx]; // discarded indexed load (emit_indexed_load_if_possible path)

    int z = (*pf, 3); // discarded deref as a comma-operator LHS
    if (z != 3)
        return 1;

    return 42;
}

// The fix redirects a REG_ZERO dest_reg to a fresh alloc_temp_reg() (11 slots,
// see NUM_TEMP_REGS in src/codegen.c), so a discarded deref now costs one
// temp register wherever it appears -- previously it cost none, since it
// just reused REG_ZERO. Regression cover for that added pressure: a
// discarded deref nested inside a many-argument call, with several other
// live temps around it.
static int discard_deref_pressure_sum(int a, int b, int c, int d, int e, int f,
                                      int g, int h) {
    return a + b + c + d + e + f + g + h;
}

[[cccc::test(return = 42)]]
int test_discarded_deref_temp_pressure(void) {
    float  fx = 7.0f;
    float *pf = &fx;
    int r = discard_deref_pressure_sum(1 + 1, 2 + 2, 3 + 3, 4 + 4, (*pf, 5) + 5,
                                       6 + 6, 7 + 7, 8 + 8);
    // (1+1)+(2+2)+(3+3)+(4+4)+(5+5)+(6+6)+(7+7)+(8+8) = 2*(1+..+8) = 72
    if (r != 72)
        return 1;
    return 42;
}

// #1174: `long double`'s size/align is platform-conditional (src/type.c) --
// on macOS arm64 `long double` IS `double` (8/8, verified against gcc-16 and
// clang), everywhere else this project supports it stays 16/16. Before this
// fix the hardcoded 16/16 was silently wrong only on macOS arm64,
// reproduced as a real ASan stack-buffer-overflow (the array-stride case
// below) since every folded `sizeof(long double)`/offset under -c=native/-m
// baked in a stride 8 bytes too wide for what the host actually allocates.
[[cccc::test(return = 42)]]
int test_ldouble_size_matches_host(void) {
#if defined(__APPLE__) && defined(__aarch64__)
    _Static_assert(sizeof(long double) == 8, "cccc");
    _Static_assert(_Alignof(long double) == 8, "cccc");
    _Static_assert(sizeof(long double _Complex) == 16, "cccc");
    _Static_assert(_Alignof(long double _Complex) == 8, "cccc");
#else
    _Static_assert(sizeof(long double) == 16, "cccc");
    _Static_assert(_Alignof(long double) == 16, "cccc");
    _Static_assert(sizeof(long double _Complex) == 32, "cccc");
    _Static_assert(_Alignof(long double _Complex) == 16, "cccc");
#endif

    // The #1174 OOB repro: a folded array stride that must match sizeof
    // exactly, or a[2] writes past what the host allocates under
    // -c=native/-m.
    long double a[3] = {0};
    a[2]             = 3.0L;
    if (a[2] != 3.0L)
        return 1;
    if ((char *)&a[2] - (char *)&a[0] != 2 * (long)sizeof(long double))
        return 2;

    struct ld_pair {
        int         i;
        long double ld;
    };
#if defined(__APPLE__) && defined(__aarch64__)
    _Static_assert(sizeof(struct ld_pair) == 16, "cccc");
#else
    _Static_assert(sizeof(struct ld_pair) == 32, "cccc");
#endif

    return 42;
}

#pragma cccc suite end

// [from test_nexttoward.c]
// Regression: nexttoward/nextafter family on all platforms (ticket #491).
#pragma cccc suite begin "floats/nexttoward"

[[cccc::test]] void test_nextafter_basic(void) {
    double a = nextafter(1.0, 2.0);
    Assert(a > 1.0);
    double b = nextafter(1.0, 0.0);
    Assert(b < 1.0);
    Assert(nextafter(a, 2.0) > a);
}

[[cccc::test]] void test_nextafter_float(void) {
    float a = nextafterf(1.0f, 2.0f);
    Assert(a > 1.0f);
    float b = nextafterf(1.0f, 0.0f);
    Assert(b < 1.0f);
}

[[cccc::test]] void test_nexttoward_matches_nextafter(void) {
    double x = 1.0;
    double y = 2.0;
    Assert(nexttoward(x, y) == nextafter(x, y));
    Assert(nexttoward(x, 0.0) == nextafter(x, 0.0));
}

[[cccc::test]] void test_nexttowardf_matches_nextafterf(void) {
    float x = 1.0f;
    Assert(nexttowardf(x, 2.0f) == nextafterf(x, 2.0f));
    Assert(nexttowardf(x, 0.0f) == nextafterf(x, 0.0f));
}

[[cccc::test]] void test_nexttowardl_matches_nextafter(void) {
    double      x = 1.0;
    long double a = nexttowardl((long double)x, (long double)2.0);
    Assert(a > (long double)1.0);
    long double b = nexttowardl((long double)x, (long double)0.0);
    Assert(b < (long double)1.0);
}

// [from test_float_comprehensive]
// Arithmetic, comparisons, unary minus, assignment chain with doubles.
static double flc_add(double a, double b) {
    return a + b;
}
static double flc_multiply(double a, double b) {
    return a * b;
}

[[cccc::test(return = 42)]]
int test_float_comprehensive(void) {
    if (flc_add(10.0, 32.0) != 42.0)
        return 1;
    if (flc_multiply(6.0, 7.0) != 42.0)
        return 2;
    if (84.0 / 2.0 != 42.0)
        return 3;
    if (50.0 - 8.0 != 42.0)
        return 4;
    double expr = (10.0 + 2.0) * 3.0 + 6.0; // (12*3)+6=42
    if (expr != 42.0)
        return 5;
    double a = 42.0, b = 41.0;
    if (!(a > b) || !(b < a) || a != 42.0)
        return 6;
    double neg = -42.0;
    if (neg != -42.0 || -neg != 42.0)
        return 7;
    double v1, v2, v3;
    v1 = v2 = v3 = 42.0;
    if (v1 != 42.0 || v2 != 42.0 || v3 != 42.0)
        return 8;
    return 42;
}

// [from test_fp_minimal]
// Function-pointer call through int-returning function.
static int fpm_add(int a, int b) {
    return a + b;
}

[[cccc::test(return = 42)]]
int test_fp_minimal(void) {
    int (*fp)(int, int) = fpm_add;
    return fp(10, 32);
}

// ND_MEMBER float/double loads (#917): gen_expr's ND_MEMBER case computes the
// member address into dest_reg and then loads through the same register.
// For a flonum member dest_reg is a float register, and FREG_A0-A7 alias
// REG_A0-A7 by raw index -- these guard the refactor that routes the
// address through a separate temp register (see man/VM.md's calling
// convention section and ND_VAR's flonum branch in src/codegen.c).
struct FM_Point {
    double x, y;
};
struct FM_Nested {
    struct FM_Point a;
    struct FM_Point b;
};
struct FM_Mixed {
    int    i;
    double d;
    float  f;
};
union FM_Union {
    double    d;
    long long i;
};

static double fm_sum2(double a, double b) {
    return a + b;
}
static float fm_sumf2(float a, float b) {
    return a + b;
}

[[cccc::test(return = 42)]]
int test_member_float_double_loads(void) {
    // Flat double/float member reads, by value and as call arguments (the
    // FREG_A0 aliasing case from the ticket).
    struct FM_Mixed m  = {.i = 10, .d = 21.0, .f = 11.0f};
    double          dv = m.d;
    float           fv = m.f;
    if (dv != 21.0 || fv != 11.0f)
        return 1;
    if (fm_sum2(m.d, 21.0) != 42.0)
        return 2;
    if (fm_sumf2(m.f, 31.0f) != 42.0f)
        return 3;

    // Mixed int/float member read in the same expression -- exercises the
    // aliasing scenario directly (integer address computation interleaved
    // with a flonum load through the same raw register index).
    if ((double)m.i + m.d != 31.0)
        return 4;

    // Member read through a pointer.
    struct FM_Mixed *p = &m;
    if (p->d != 21.0)
        return 5;

    // Nested struct member chain.
    struct FM_Nested n = {.a = {.x = 10.0, .y = 5.0},
                          .b = {.x = 20.0, .y = 7.0}};
    if (n.a.x + n.b.x != 30.0)
        return 6;
    struct FM_Nested *np = &n;
    if (np->a.x + np->b.x != 30.0)
        return 7;

    // Array-of-struct element members.
    struct FM_Point pts[3] = {{1.0, 1.0}, {2.0, 2.0}, {3.0, 3.0}};
    int             idx    = 1;
    if (pts[idx].x != 2.0 || pts[idx].y != 2.0)
        return 8;

    // Union float member.
    union FM_Union u;
    u.d = 42.0;
    if (u.d != 42.0)
        return 9;

    // Deeply nested float member expression: exercises peak temp-register
    // use through the binary-op spill path (TEMP_REG_SPILL_THRESHOLD).
    struct FM_Point a = {1.0, 2.0}, b = {3.0, 4.0}, c = {5.0, 6.0},
                    d = {7.0, 8.0}, e = {9.0, 1.0}, f = {2.0, 3.0};
    double          deep =
        a.x * b.y + c.x * d.y + e.x * f.y - a.y * b.x - c.y * d.x - e.y * f.x;
    // 1*4 + 5*8 + 9*3 - 2*3 - 6*7 - 1*2 = 4+40+27-6-42-2 = 21
    if (deep != 21.0)
        return 10;

    // Long member-chain through nested pointers.
    struct FM_Nested  chain = {.a = {.x = 40.0, .y = 0.0},
                               .b = {.x = 2.0, .y = 0.0}};
    struct FM_Nested *cp    = &chain;
    if (cp->a.x + cp->b.x != 42.0)
        return 11;

    return 42;
}

#pragma cccc suite end
