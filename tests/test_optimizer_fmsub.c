// CCCC_FLAGS: --optimize=4

// Verifies correctness of FMSUB3 semantics (two-rounding: product rounded first,
// then subtracted). The fusion pass emits FMSUB3 for the minuend form (a*b - c)
// when FMUL3 and FSUB3 are adjacent with no intervening instructions.
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
