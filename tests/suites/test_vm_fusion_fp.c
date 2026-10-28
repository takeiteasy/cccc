// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: a host compiler may contract a*b+c into one fused op, which the VM never does
// Fused FP multiply-add rounds twice, like the multiply and add it replaces.
// The operands are chosen so a single-rounding fma gives a different answer.

#include <math.h>

volatile double fp_a, fp_b, fp_c, fp_d;
volatile float  fp_fa, fp_fb, fp_fc;

static void fp_setup(void) {
    fp_a  = 1.0 + 1.0 / 134217728.0; // 1 + 2^-27
    fp_b  = 1.0 - 1.0 / 134217728.0; // 1 - 2^-27, a*b = 1 - 2^-54 rounds to 1
    fp_fa = 1.0f + 1.0f / 8192.0f;   // 1 + 2^-13
    fp_fb = 1.0f - 1.0f / 8192.0f;   // a*b = 1 - 2^-26 rounds to 1
}

[[cccc::test]]
void test_operands_discriminate_fma(void) {
    fp_setup();
    AssertTrue(fma(fp_a, fp_b, -1.0) != 0.0);
    AssertTrue(fmaf(fp_fa, fp_fb, -1.0f) != 0.0f);
}

[[cccc::test]]
void test_double_multiply_add_rounds_twice(void) {
    fp_setup();
    fp_c       = -1.0;
    double sum = fp_a * fp_b + fp_c;
    AssertTrue(sum == 0.0);
    sum = fp_c + fp_a * fp_b;
    AssertTrue(sum == 0.0);
}

[[cccc::test]]
void test_double_multiply_sub_rounds_twice(void) {
    fp_setup();
    fp_c     = 1.0;
    double d = fp_a * fp_b - fp_c;
    AssertTrue(d == 0.0);
    d = fp_c - fp_a * fp_b;
    AssertTrue(d == 0.0);
}

[[cccc::test]]
void test_float_multiply_add_rounds_twice(void) {
    fp_setup();
    fp_fc   = -1.0f;
    float s = fp_fa * fp_fb + fp_fc;
    AssertTrue(s == 0.0f);
    s = fp_fc + fp_fa * fp_fb;
    AssertTrue(s == 0.0f);
    fp_fc = 1.0f;
    s     = fp_fa * fp_fb - fp_fc;
    AssertTrue(s == 0.0f);
    s = fp_fc - fp_fa * fp_fb;
    AssertTrue(s == 0.0f);
}

[[cccc::test]]
void test_two_products_and_mixed_widths(void) {
    fp_setup();
    fp_c = 3.0;
    fp_d = 4.0;
    // Each product is rounded before the add or subtract.
    double r = fp_a * fp_b + fp_c * fp_d;
    AssertTrue(r == 1.0 + 12.0);
    r = fp_c * fp_d - fp_a * fp_b;
    AssertTrue(r == 12.0 - 1.0);
    // float * float widens to double only after it is rounded to float.
    double w = fp_c + fp_fa * fp_fb;
    AssertTrue(w == 3.0 + 1.0);
}

[[cccc::test]]
void test_dot_product_and_polynomial(void) {
    double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
    double acc = 0;
    for (int i = 0; i < 4; i++)
        acc = acc + x[i] * y[i];
    AssertTrue(acc == 70.0);
    float fx[3] = {0.5f, 1.5f, 2.5f};
    float fa    = 0;
    for (int i = 0; i < 3; i++)
        fa = fx[i] * fx[i] - fa;
    AssertTrue(fa == 6.25f - (2.25f - 0.25f));
    double v = 2.0, p = 1.0;
    p = p * v + 3.0;
    p = p * v + 4.0;
    AssertTrue(p == 14.0);
}
