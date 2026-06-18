// CCCC_FLAGS: --optimize=4

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
        sum = sum - prod;
    }
    return sum;
}

// Pattern: diff -= scale * value  (common in numerical methods)
static double weighted_diff(double init, double scale, double v1, double v2) {
    double result = init;
    result -= scale * v1;
    result -= scale * v2;
    return result;
}

// mandelbrot inner loop exercises both FMADD3 and FNMSUB3:
//   zr_next = zr*zr - zi*zi + cr  → FMSUB3 (minuend form: zr*zr - zi*zi)
//   zi_next = 2*zr*zi + ci        → FMADD3
//   escape check: zr*zr + zi*zi   → FMADD3
static int mandel_iters(double cr, double ci, int max) {
    double zr = 0.0, zi = 0.0;
    for (int i = 0; i < max; i++) {
        double zr2 = zr * zr - zi * zi + cr;
        double zi2 = 2.0 * zr * zi + ci;
        zr = zr2;
        zi = zi2;
        if (zr * zr + zi * zi > 4.0)
            return i;
    }
    return max;
}

int main(void) {
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
    if (mandel_iters(0.0, 0.0, 100) != 100)
        return 5;

    // (2,2) escapes on first iteration
    if (mandel_iters(2.0, 2.0, 100) != 0)
        return 6;

    // (-2,0) is on the boundary — reaches max
    if (mandel_iters(-2.0, 0.0, 200) != 200)
        return 7;

    return 42;
}
